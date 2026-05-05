#!/usr/bin/env python3
import argparse
import ctypes
import os
import sys


PRT_BREADCRUMB_MAGIC = 0x505254424352554D
PRT_BREADCRUMB_ANY_U32 = 0xFFFFFFFF
PRT_BREADCRUMB_SLOT_COUNT = 64

KIND_NAMES = {
    0: "none",
    1: "runtime",
    2: "dma",
    3: "spm_xlate",
    4: "rr",
    5: "gemmini",
}

PHASE_NAMES = {
    0: "none",
    1: "runtime_init_begin",
    2: "runtime_init_done",
    100: "dma_page_begin",
    101: "dma_submit_begin",
    102: "dma_rr_postcheck",
    103: "dma_doneflag_end",
    104: "dma_program_begin",
    105: "dma_program_post_fence",
    106: "dma_program_post_dst",
    107: "dma_program_post_src",
    108: "dma_wait_before_fence",
    109: "dma_wait_after_fence",
    110: "dma_wait_after_shared_fence",
    111: "dma_wait_after_release",
    112: "dma_page_end",
    113: "dma_wait_after_complete",
    114: "dma_wait_after_trace_complete",
    115: "dma_submitwait_after_wait",
    116: "dma_submitwait_after_cleanup",
    117: "dma_wait_before_shared_fence",
    118: "dma_page_after_submitwait_return",
    119: "dma_page_after_page_end",
    120: "dma_page_after_accounting",
    121: "dma_page_before_v2p",
    122: "dma_page_after_v2p",
    123: "dma_page_direct_path_decided",
    200: "spm_xlate_flush_begin",
    201: "spm_xlate_flush_end",
    202: "spm_xlate_release_fence_begin",
    203: "spm_xlate_release_fence_end",
    204: "spm_xlate_release_begin",
    205: "spm_xlate_release_end",
    206: "spm_xlate_restore_begin",
    207: "spm_xlate_restore_end",
    300: "rr_acquire_before_csr_write",
    301: "rr_acquire_after_csr_write",
    302: "rr_acquire_after_csr_read",
    303: "rr_acquire_before_set_opc",
    304: "rr_acquire_after_set_opc",
    305: "rr_acquire_before_return",
    306: "rr_acquire_after_call",
    307: "rr_release_begin",
    308: "rr_release_end",
    309: "rr_release_after_csr_write",
    400: "gemmini_pointwise_call_begin",
    401: "gemmini_pointwise_call_return",
    402: "gemmini_pointwise_matmul_begin",
    403: "gemmini_pointwise_matmul_return",
    404: "gemmini_pointwise_postcall_fence_begin",
    405: "gemmini_pointwise_postcall_rr_fence_return",
    406: "gemmini_pointwise_postcall_gemmini_fence_return",
    407: "gemmini_pointwise_postcall_drain_return",
    408: "gemmini_pointwise_postcall_release_return",
    409: "gemmini_pointwise_precall_after_scope_marker",
    410: "gemmini_pointwise_precall_after_binding_snapshot",
    411: "gemmini_pointwise_precall_dispatch_decided",
    412: "gemmini_pointwise_precall_after_postflush_snapshot",
    413: "gemmini_pointwise_precall_ready",
}

FALLBACK_TYPE_NAMES = {
    0: "OS",
    1: "WS",
    2: "CPU",
}

GEMMINI_PHASES = {
    400,
    401,
    402,
    403,
    404,
    405,
    406,
    407,
    408,
    409,
    410,
    411,
    412,
    413,
}


def fmt_u32(value: int) -> str:
    return "any" if value == PRT_BREADCRUMB_ANY_U32 else str(value)


class BreadcrumbSlot(ctypes.LittleEndianStructure):
    _fields_ = [
        ("seq", ctypes.c_uint64),
        ("mono_ns", ctypes.c_uint64),
        ("src_addr", ctypes.c_uint64),
        ("dst_addr", ctypes.c_uint64),
        ("aux_u64_0", ctypes.c_uint64),
        ("aux_u64_1", ctypes.c_uint64),
        ("kind", ctypes.c_uint32),
        ("phase", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("rc", ctypes.c_int32),
        ("line", ctypes.c_uint32),
        ("slot_idx", ctypes.c_uint32),
        ("segment_idx", ctypes.c_uint32),
        ("global_stage_id", ctypes.c_uint32),
        ("local_stage_id", ctypes.c_uint32),
        ("subbatch_id", ctypes.c_uint32),
        ("tensor_id", ctypes.c_uint32),
        ("token_id", ctypes.c_uint32),
        ("manager_id", ctypes.c_uint32),
        ("page_idx", ctypes.c_uint32),
        ("reserved0", ctypes.c_uint32),
    ]


class BreadcrumbFile(ctypes.LittleEndianStructure):
    _fields_ = [
        ("magic", ctypes.c_uint64),
        ("version", ctypes.c_uint32),
        ("header_bytes", ctypes.c_uint32),
        ("slot_count", ctypes.c_uint32),
        ("enabled", ctypes.c_uint32),
        ("filter_segment", ctypes.c_uint32),
        ("filter_global_stage", ctypes.c_uint32),
        ("filter_local_stage", ctypes.c_uint32),
        ("filter_subbatch", ctypes.c_uint32),
        ("filter_stage_radius", ctypes.c_uint32),
        ("filter_subbatch_radius", ctypes.c_uint32),
        ("last_update_ns", ctypes.c_uint64),
        ("update_count", ctypes.c_uint32),
        ("last_slot_idx", ctypes.c_uint32),
        ("last_kind", ctypes.c_uint32),
        ("last_phase", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 8),
        ("slots", BreadcrumbSlot * PRT_BREADCRUMB_SLOT_COUNT),
    ]


def decode(path: str) -> BreadcrumbFile:
    with open(path, "rb") as handle:
        blob = handle.read()
    if len(blob) < ctypes.sizeof(BreadcrumbFile):
        raise RuntimeError(
            f"breadcrumb file too small: {len(blob)} < {ctypes.sizeof(BreadcrumbFile)} bytes"
        )
    return BreadcrumbFile.from_buffer_copy(blob[: ctypes.sizeof(BreadcrumbFile)])


def slot_is_populated(slot: BreadcrumbSlot) -> bool:
    return slot.seq != 0 and (slot.seq % 2 == 0)


def print_slot(slot: BreadcrumbSlot) -> None:
    line = (
        "slot={slot} kind={kind} phase={phase} seq={seq} mono_ns={mono_ns} "
        "seg={seg} gstage={gstage} lstage={lstage} sb={sb} tensor={tensor} "
        "tok={tok} mgr={mgr} page={page} rc={rc} flags=0x{flags:x} "
        "src=0x{src:x} dst=0x{dst:x} aux0=0x{aux0:x} aux1=0x{aux1:x} line={line}".format(
            slot=slot.slot_idx,
            kind=KIND_NAMES.get(slot.kind, f"kind_{slot.kind}"),
            phase=PHASE_NAMES.get(slot.phase, f"phase_{slot.phase}"),
            seq=slot.seq,
            mono_ns=slot.mono_ns,
            seg=fmt_u32(slot.segment_idx),
            gstage=fmt_u32(slot.global_stage_id),
            lstage=fmt_u32(slot.local_stage_id),
            sb=fmt_u32(slot.subbatch_id),
            tensor=fmt_u32(slot.tensor_id),
            tok=slot.token_id,
            mgr=fmt_u32(slot.manager_id),
            page=fmt_u32(slot.page_idx),
            rc=slot.rc,
            flags=slot.flags,
            src=slot.src_addr,
            dst=slot.dst_addr,
            aux0=slot.aux_u64_0,
            aux1=slot.aux_u64_1,
            line=slot.line,
        )
    )
    if slot.kind == 5 and slot.phase in GEMMINI_PHASES:
        dim_j = (slot.aux_u64_1 >> 32) & 0xFFFFFFFF
        dim_k = slot.aux_u64_1 & 0xFFFFFFFF
        fallback = FALLBACK_TYPE_NAMES.get(slot.token_id, str(slot.token_id))
        line += (
            " detail=input=0x{inp:x} output=0x{out:x} weights=0x{weights:x} "
            "dim_J={dim_j} dim_K={dim_k} fallback={fallback}".format(
                inp=slot.src_addr,
                out=slot.dst_addr,
                weights=slot.aux_u64_0,
                dim_j=dim_j,
                dim_k=dim_k,
                fallback=fallback,
            )
        )
    print(line)


def main() -> int:
    parser = argparse.ArgumentParser(description="Decode pipeline-runtime breadcrumb binary")
    parser.add_argument("path", help="breadcrumb binary path")
    parser.add_argument("--all", action="store_true", help="print all populated slots")
    parser.add_argument("--slot", type=int, default=None, help="print only one slot")
    args = parser.parse_args()

    data = decode(args.path)
    if data.magic != PRT_BREADCRUMB_MAGIC:
      print(f"unexpected magic: 0x{data.magic:x}", file=sys.stderr)
      return 1

    print(f"path={os.path.abspath(args.path)}")
    print(
        "version={version} enabled={enabled} slot_count={slots} update_count={updates} "
        "last_slot={last_slot} last_kind={last_kind} last_phase={last_phase} last_update_ns={last_ns}".format(
            version=data.version,
            enabled=data.enabled,
            slots=data.slot_count,
            updates=data.update_count,
            last_slot=data.last_slot_idx,
            last_kind=KIND_NAMES.get(data.last_kind, f"kind_{data.last_kind}"),
            last_phase=PHASE_NAMES.get(data.last_phase, f"phase_{data.last_phase}"),
            last_ns=data.last_update_ns,
        )
    )
    print(
        "filters segment={seg} global_stage={gstage} local_stage={lstage} subbatch={sb} "
        "stage_radius={sr} subbatch_radius={sbr}".format(
            seg=fmt_u32(data.filter_segment),
            gstage=fmt_u32(data.filter_global_stage),
            lstage=fmt_u32(data.filter_local_stage),
            sb=fmt_u32(data.filter_subbatch),
            sr=data.filter_stage_radius,
            sbr=data.filter_subbatch_radius,
        )
    )

    if args.slot is not None:
        if args.slot < 0 or args.slot >= data.slot_count:
            print(f"slot out of range: {args.slot}", file=sys.stderr)
            return 1
        slot = data.slots[args.slot]
        if not slot_is_populated(slot):
            print(f"slot {args.slot} is empty")
            return 0
        print_slot(slot)
        return 0

    if data.last_slot_idx < data.slot_count and slot_is_populated(data.slots[data.last_slot_idx]):
        print("last_slot:")
        print_slot(data.slots[data.last_slot_idx])

    if args.all:
        print("all_slots:")
        populated_slots = [slot for slot in data.slots if slot_is_populated(slot)]
        # Breadcrumb slot seq is slot-local because notes are sharded across slots.
        # For cross-slot frontier reading, monotonic timestamp is the nearest
        # available global order.
        populated_slots.sort(key=lambda slot: (slot.mono_ns, slot.slot_idx, slot.seq))
        for slot in populated_slots:
            print_slot(slot)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
