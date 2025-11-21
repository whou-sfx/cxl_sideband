#########################################################################
# File Name: frace.sh
# Desc:
# Author: Andy-wei.hou
# mail: wei.hou@scaleflux.com
# Created Time: 2025年11月21日 星期五 22时17分17秒
# Log: kk
#########################################################################
#!/bin/bash
# ftrace_trace.sh
# 用法: sudo ./ftrace_trace.sh <kernel_function_symbol>
# 例: sudo ./ftrace_trace.sh mctp_route_lookup

if [ "$EUID" -ne 0 ]; then
    echo "请使用 root 运行此脚本"
    exit 1
fi

if [ $# -ne 1 ]; then
    echo "用法: $0 <kernel_function_symbol>"
    exit 1
fi

FUNC=$1
EVENT_NAME="trace_$FUNC"

TRACING_DIR="/sys/kernel/debug/tracing"

# 1. 禁用 ftrace
echo 0 > /sys/kernel/debug/tracing/events/kprobes/enable
echo 0 > $TRACING_DIR/tracing_on
echo > $TRACING_DIR/trace
echo > $TRACING_DIR/kprobe_events

# 2. 配置 kprobe 追踪函数返回值
# r: kretprobe, 只捕获返回值
echo "r:$EVENT_NAME $FUNC retval=%ax" > $TRACING_DIR/kprobe_events

# 3. 使能 kprobe 事件
echo 1 > $TRACING_DIR/events/kprobes/$EVENT_NAME/enable

# 4. 启用 ftrace
echo 1 > $TRACING_DIR/tracing_on

echo "开始追踪函数 $FUNC，输出打印到 trace_pipe ..."
echo "按 Ctrl+C 停止"

# 5. Dump 输出
cat $TRACING_DIR/trace_pipe

