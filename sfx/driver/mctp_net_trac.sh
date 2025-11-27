sudo bpftrace -e '
kprobe:mctp_* {
    printf("ENTER %-30s arg0=%lx arg1=%lx arg2=%lx\n", func, arg0, arg1, arg2);
}
kretprobe:mctp_* {
    printf("EXIT  %-30s ret=%lx\n", func, retval);
}'

