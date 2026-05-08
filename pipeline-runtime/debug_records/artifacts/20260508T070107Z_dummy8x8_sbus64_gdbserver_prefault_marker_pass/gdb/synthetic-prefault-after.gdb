printf "\n--- synthetic prefault after-prefault reached ---\n"
printf "hit_bpnum="
print $_hit_bpnum
bt 12
info args
info locals
print blob_size
print step
print total_pages
print touched_pages
print buf
print touch
x/20i $pc-40
detach
