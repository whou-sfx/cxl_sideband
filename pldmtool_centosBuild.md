# pldmtool Build Guide on CentOS 10

## 背景

CentOS 10 与 Ubuntu 的差异导致直接套用 Ubuntu 的构建命令会遇到多处问题，本文记录所有坑点和对应修复方法。

---

## 构建流程总览

```
1. 升级 meson（系统版本太旧）
2. 安装 Python 依赖（dnf 没有，用 pip）
3. 编译安装 libpldm（--prefix=/usr）
4. 安装 C++ 头文件依赖（stdexec、function2、CLI11）
5. 编译安装 pldm/pldmtool（--prefix=/usr）
6. 更新动态链接器缓存
7. 启动 dbus 服务
8. 验证运行
```

---

## Step 1：升级 meson

CentOS 10 系统自带 meson 版本（约 1.4.1）过旧，`libpldm` 要求 >= 1.6.0。

```bash
pip3 install --upgrade meson --user

# 确认 PATH（~/.local/bin 必须在 /usr/bin 前面）
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

meson --version   # 应显示 >= 1.6.0（如 1.11.1）
```

> **注意**：`sudo meson` 与 `meson` 版本不同，因为 sudo 不继承用户 PATH，
> 且 Python 以 root 身份运行时查找的是系统 site-packages 而非用户 site-packages。
> 凡需要 sudo 的操作，必须用绝对路径：`sudo ~/.local/bin/meson ...`

---

## Step 2：安装 Python 依赖

CentOS 10 的 dnf 没有 `python3-pytest`、`python3-mako` 等包，用 pip 安装：

```bash
pip3 install mako inflection pyyaml pytest --user
```

对比：Ubuntu 用 `sudo apt install python3-mako python3-inflection python3-yaml python3-pytest`，CentOS 10 全部改用 pip。

---

## Step 3：编译安装 libpldm

```bash
git clone https://github.com/openbmc/libpldm
cd libpldm

# 关键：指定 --prefix=/usr，避免安装到 /usr/local 导致路径不匹配
~/.local/bin/meson setup build --reconfigure --prefix=/usr -Dtests=disabled
~/.local/bin/meson compile -C build
sudo ~/.local/bin/meson install -C build

# 更新动态链接器缓存
sudo ldconfig
```

> **为什么必须 `--prefix=/usr`？**
> pldmtool 内部的 `InstanceIdDb` 构造函数调用 `pldm_instance_db_init_default`，
> 该函数硬编码查找 `/usr/share/libpldm/instance-db/default`。
> 如果 libpldm 安装到 `/usr/local`，instance-db 文件在 `/usr/local/share/...`，
> 路径不匹配导致抛出 `std::error_condition` 崩溃。
>
> 若已经安装到 `/usr/local`，可用符号链接临时修复：
> ```bash
> sudo mkdir -p /usr/share/libpldm
> sudo ln -s /usr/local/share/libpldm/instance-db /usr/share/libpldm/instance-db
> ```

---

## Step 4：安装 pldm 的 C++ 头文件依赖

pldm 依赖多个 C++ 库，CentOS 10 的 dnf 均没有，需要从子项目或源码安装。

### 4.1 stdexec（exec/async_scope.hpp）

新版 sdbusplus 依赖 stdexec。pldm 的 sdbusplus 子项目编译时已经下载了 stdexec，直接安装头文件：

```bash
# 找 stdexec 子项目位置
find ~/whou/src/pldm/subprojects -path "*/stdexec/include" -type d 2>/dev/null
# 通常在 subprojects/sdbusplus/subprojects/stdexec/include 或 subprojects/stdexec/include

# 安装头文件（纯头文件库）
sudo cp -r <上面找到的路径>/exec    /usr/local/include/
sudo cp -r <上面找到的路径>/stdexec /usr/local/include/

# 验证
ls /usr/local/include/exec/async_scope.hpp
```

> **为什么会出现这个依赖？**
> reconfigure 后 meson 优先使用系统已安装的 sdbusplus（`/usr/local/include/sdbusplus/`），
> 这个版本引入了 `async/execution.hpp` → 需要 stdexec 头文件。
> 使用 `--wrap-mode=forcefallback` 强制用子项目虽然可以绕过，但会导致 fmt/stdplus 等
> 子项目的级联编译失败，不推荐。

### 4.2 function2

Ubuntu 的 `libfunction2-dev` 在 CentOS 10 不存在，从子项目构建：

```bash
# 使用 pldm 已下载的子项目
cd ~/whou/src/pldm/subprojects/function2
mkdir -p build && cd build
cmake .. -DBUILD_TESTING=OFF
cmake --build .
sudo cmake --install .
```

### 4.3 CLI11（impl/Encoding_inl.hpp）

系统中已安装的 CLI11 可能不完整（缺少 `impl/` 子目录）。从子项目重新安装：

```bash
# 检查是否缺失
ls /usr/local/include/CLI/impl/ 2>/dev/null || echo "impl/ 缺失"

# 从 pldm 子项目覆盖安装完整版本
sudo cp -r ~/whou/src/pldm/subprojects/CLI11/include/CLI /usr/local/include/

# 验证
ls /usr/local/include/CLI/impl/Encoding_inl.hpp
```

---

## Step 5：编译安装 pldm / pldmtool

```bash
git clone https://github.com/openbmc/pldm.git
cd pldm

# 同样指定 --prefix=/usr
~/.local/bin/meson setup build --reconfigure \
    --prefix=/usr \
    -Dtransport-implementation=af-mctp \
    -Dtests=disabled

~/.local/bin/meson compile -C build
sudo ~/.local/bin/meson install -C build
sudo ldconfig
```

> **CentOS 10 没有 gmock/gtest**：dnf 包名为 `gmock-devel gtest-devel`（不是 Ubuntu 的 `libgmock-dev`），
> 或者直接加 `-Dtests=disabled` 跳过测试依赖。

---

## Step 6：启动 dbus 服务

pldmtool 运行时需要 D-Bus：

```bash
sudo systemctl enable dbus
sudo systemctl start dbus
sudo systemctl status dbus   # 确认 Active: active (running)
```

---

## Step 7：验证

```bash
sudo pldmtool --version

# 测试发送 PLDM 命令（需要 mctp 设备已就绪）
sudo pldmtool base GetTID -m 8 -v
sudo pldmtool base GetPLDMVersion -m 8 -t 0 -v
```

---

## 常见错误速查

| 错误信息 | 原因 | 修复 |
|---------|------|------|
| `Program 'pytest' not found` | CentOS 10 dnf 没有 pytest | `pip3 install pytest --user` 或 `meson setup -Dtests=disabled` |
| `python3 is missing modules: mako` | dnf 没有 python3-mako | `pip3 install mako inflection pyyaml --user` |
| `Meson version is 1.4.1 but project requires >=1.6.0` | 系统 meson 太旧 | `pip3 install --upgrade meson --user` |
| `sudo meson` 版本仍是旧版 | sudo 用系统 Python，找不到用户 pip 包 | 用 `sudo ~/.local/bin/meson ...` |
| `libpldm.so.0: cannot open shared object file` | ldconfig 未更新 | `sudo ldconfig` 或 `echo '/usr/local/lib64' \| sudo tee /etc/ld.so.conf.d/local-lib64.conf && sudo ldconfig` |
| `terminate called after throwing an instance of 'std::error_condition'` | instance-db 路径不匹配（安装到 /usr/local） | 重新编译加 `--prefix=/usr`，或创建符号链接 |
| `No match for argument: libgmock-dev` | Ubuntu 包名，CentOS 10 不同 | `sudo dnf install gmock-devel gtest-devel` 或 `-Dtests=disabled` |
| `fatal error: exec/async_scope.hpp: No such file or directory` | stdexec 头文件未安装，sdbusplus 新版依赖 | 从 sdbusplus 子项目复制头文件到 `/usr/local/include/` |
| `No match for argument: libfunction2-dev` | Ubuntu 专有包名，CentOS 10 不存在 | 从 `subprojects/function2` 用 cmake 编译安装 |
| `fatal error: impl/Encoding_inl.hpp: No such file or directory` | 系统 CLI11 安装不完整，缺少 `impl/` 子目录 | 从 `subprojects/CLI11` 覆盖安装完整头文件 |
| `Cannot initialize mctp-demux transport layer` | pldmtool 未用 af-mctp 编译，或旧版 binary | 确认用 `-Dtransport-implementation=af-mctp` 重新编译 |

---

## mctp userspace toolkit（CodeConstruct/mctp）

```bash
git clone https://github.com/CodeConstruct/mctp
cd mctp

# mctp meson.build 中 struct mctp_fq_addr / IFLA_MCTP_PHYS_BINDING 检测失败为 NO
# 属于内核头文件版本较低，不影响主体功能编译

~/.local/bin/meson setup build --reconfigure -Dtests=disabled
~/.local/bin/meson compile -C build
sudo ~/.local/bin/meson install -C build
```

> CentOS 10 内核头文件版本可能不含最新 MCTP 结构体（`struct mctp_fq_addr`、`IFLA_MCTP_PHYS_BINDING`），
> meson 检测结果为 NO，对应功能会被裁剪，不阻塞整体构建。
