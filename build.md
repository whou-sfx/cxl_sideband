## env
10.33.118.41   现在是连接total phase和logical analyzer
10.33.118.71   现在是连接Haps，用来program和uart

## mctp userspace toolkit
git clone https://github.com/CodeConstruct/mctp

sudo apt install -y python3-pytest

meson setup build --reconfigure
meson compile -C build
meson install -C build



### pldm build
###Clone the repo
git clone https://github.com/openbmc/pldm.git


## install dependency
sudo apt-get install -y pipx
pipx install meson
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

sudo apt-get install cmake pkg-config git  systemd-dev

sudo apt-get install -y libsystemd-dev
sudo apt install python3-inflection python3-mako python3-yaml


## Build pldm
cd plmd

meson setup build -Dtransport-implementation=af-mctp --reconfigure

meson setup build -Dtransport-implementation=af-mctp -Dbuildtype=debug --reconfigure


meson setup build -Dtransport-implementation=af-mctp -Dbuildtype=debug -Dtests=disabled --reconfigure



##disable systemd

meson setup build -Dtransport-implementation=af-mctp -Dbuildtype=debug -Dtests=disabled -Dsystemd=disabled --reconfigure



##libpldm
sudo apt install -y libgmock-dev libgtest-dev

git clone https://github.com/openbmc/libpldm
cd libpldm

meson setup build --reconfigure
meson compile -C build
meson install -C build
#apply the ld link directory
sudo ldconfig

### function2
sudo apt install libfunction2-dev

#### or
cd subprojects/function2
mkdir -p build && cd build
cmake .. -DBUILD_TESTING=OFF
cmake --build .
sudo cmake --install .





sudo apt install -y libdbus-1-dev


Notes about the build


~~~

#ccheck meson configuration

meson configure build >build.cfg
vim ./build.cfg
~~~

~~~
In file included from ../subprojects/sdbusplus/src/bus/match.cpp:3:                                                                                           
../subprojects/sdbusplus/include/sdbusplus/bus/match.hpp: In function ‘constexpr auto sdbusplus::bus::match::rules::type::signal()’:                          
../subprojects/sdbusplus/include/sdbusplus/bus/match.hpp:62:16: error: invalid return type ‘std::__cxx11::basic_string<char>’ of ‘constexpr’ function ‘constexpr auto sdbusplus::bus::match::rules::type::signal()’                                             


# 修改 meson.build 文件中的 C++ 标准
# 找到类似这样的行：
# cpp_std = 'c++23'
# 改为：
cpp_std = 'c++20'
~~~



## pldmtool running env setup

### 1. Need start dbus service
sudo systemctl enable dbus
sudo systemctl start dbus
sudo systemctl status dbus



sudo pldmtool raw -d /dev/mctp/mctp_bridge0 -s 19 -t 8 0x02 0x01

## Useful dgb debug command
gdb --args build/pldmtool raw
# gdb>
(gdb) catch throw
(gdb) run
# 当 gdb 在 throw 停住后：
(gdb) bt
# 找到抛出的帧（应该在 InstanceIdDb 构造或 sdbusplus）
# 查看异常对象（常见技巧，若异常类型是 std::error_condition）
(gdb) info threads
(gdb) frame <num>   # 切到抛出那一帧
(gdb) list          # 查看源代码上下文（如果有调试符号）

# 在构造函数调用处停在调用前（第 22 行）
(gdb) break ../common/instance_id.hpp:22
(gdb) run           # 重新运行程序
# 程序会停在第22行（在调用 pldm_instance_db_init_default 之前）
(gdb) next          # 执行那一行（即调用完成并返回）
(gdb) print rc      # 显示返回码




## pldmtool test example
sudo pldmtool base GetPLDMVersion -m 8 -t 0 -v

pldmtool: Tx: 81 00 03 00 00 00 00 01 00
pldmtool: Rx: 01 00 03 00 00 00 00 01 00 01 01 01 01
{
    "CompletionCode": "SUCCESS",
    "Response": "01.01.01\u0001"
}

sudo pldmtool base GetTID -m 8 -v

pldmtool: Tx: 81 00 02
pldmtool: Rx: 01 00 02 00 42
{
    "Response": 66
}



#### PLDM Image升级

##查询设备标识符
 sudo pldmtool fw_update QueryDeviceIdentifiers -m 8 -v

##获取固件参数
sudo pldmtool fw_update GetFwParams -m 8 -v 



### debug by gdb
tcnsh@ubuntu22:~/whou/src/cxl_sideband/openbmc/pldm$ sudo gdb --args build/pldmtool/pldmtool base GetPLDMVersion -m 8 -t 0 -v
GNU gdb (Ubuntu 15.0.50.20240403-0ubuntu1) 15.0.50.20240403-git
Copyright (C) 2024 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
Type "show copying" and "show warranty" for details.
This GDB was configured as "x86_64-linux-gnu".
Type "show configuration" for configuration details.
For bug reporting instructions, please see:
<https://www.gnu.org/software/gdb/bugs/>.
Find the GDB manual and other documentation resources online at:
    <http://www.gnu.org/software/gdb/documentation/>.

For help, type "help".
Type "apropos word" to search for commands related to "word"...
Reading symbols from build/pldmtool/pldmtool...
(gdb) run
Starting program: /home/tcnsh/whou/src/cxl_sideband/openbmc/pldm/build/pldmtool/pldmtool base GetPLDMVersion -m 8 -t 0 -v
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".
warning: could not find '.gnu_debugaltlink' file for /lib/x86_64-linux-gnu/libcap.so.2
pldmtool: Tx: 81 00 03 00 00 00 00 01 00
pldmtool: Rx: 01 00 03 00 00 00 00 01 00 01 00 00 00
{
    "CompletionCode": "SUCCESS",
    "Response": "00.00.00\u0001"
}
[Inferior 1 (process 296507) exited normally]
(gdb)


[RECV 14 bytes]:
01 08 13 C8 01 81 00 03 00 00 00 00 01 00
hdr[ver: 1, dst: 8, src:13, flags_tag: c8], msg_type: 1, len: e]

[SEND 18 bytes]
01 13 08 C0 01 01 00 03 00 00 00 00 01 00 01 00
00 00

01 13 08 C0 01 01 00 03 00 00 00 00 01 00 01 00

01 13 08 C0 01 01 00 03 00 01 00 00 00 01 00



## cxl cci build
git clone https://github.com/computexpresslink/libcxlmi.git
cd libcxlmi
meson setup build
meson compile -C build

tcnsh@ubuntu22:~/whou/src/cxl_sideband/cci/libcxlmi$ sudo ./build/examples/cxl-mctp 1 8
ep 1:8
libcxlmi: Unexpected fixed length of response. 12 30
libcxlmi: Unexpected fixed length of response. 12 30
libcxlmi: Unexpected fixed length of response. 12 20
libcxlmi: Unexpected minimum length of response
libcxlmi: Payload length not matching expected part of full message 0 8
libcxlmi: Unexpected fixed length of response. 12 20
libcxlmi: Unexpected minimum length of response
libcxlmi: Unexpected minimum length of response
-------------------------------------------------
