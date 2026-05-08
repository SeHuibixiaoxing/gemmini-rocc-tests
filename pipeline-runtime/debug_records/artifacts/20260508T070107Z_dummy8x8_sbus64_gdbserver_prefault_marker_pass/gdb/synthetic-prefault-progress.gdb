printf "\n--- synthetic prefault progress stop ---\n"
printf "hit_bpnum="
print $_hit_bpnum
bt 12
info args
info locals
print off
print touched_pages
print blob_size
print step
print total_pages
print next_progress_bytes
print buf
print touch
print &touch[off]
x/16xb &touch[off]
x/20i $pc-40
