# NETFS：SMB2/3 与 NFS 网络文件浏览

wLaunchELF R3Z 中文版（v4.8）内置 NETFS 支持，可在文件浏览器中直接访问
局域网内的 SMB 共享（Samba）与 NFS 导出目录，无需通过 host:/udpfs: 中转。

## 设备入口

- `SMB:` —— SMB2/3 共享。进入根目录会列出 `SMB.CNF` 中已配置的全部共享名；
  进入某个共享名即浏览该共享内的文件。
- `nfs:` —— NFS 导出。进入根目录列出 `NFS.CNF` 中已配置的共享名。

两者在「设置 → SMB 网络设置」中配置，也可手动编辑配置文件。

## 配置文件格式

配置文件保存于 wLaunchELF 的配置路径（通常为 `mc0:/SYS-CONF/SMB.CNF`，
找不到时回退到 `mc1:` 与 `mass:`）。每行一个 `键=值`，键名如下：

```
NAME=我的共享
URL=smb://192.168.1.10/share
USERNAME=guest
PASSWORD=
```

- `NAME`    —— 在 `SMB:` 根目录中显示的名称。
- `URL`     —— SMB 共享地址，格式 `smb://<服务器>/<共享>`。
- `USERNAME`/`PASSWORD` —— 访问凭据，匿名共享可留空。

`nfs:` 使用对应的 `NFS.CNF`，`URL` 格式为 `nfs://<服务器>/<导出路径>`。

## 使用前提

- 需要以太网（DEV9 + SMAP）物理连接，且主机与文件服务器在同一局域网。
- `SMB 网络设置`保存后，首次进入 `SMB:`/`nfs:` 会自动初始化网络栈
  （NETMAN + PS2IP）并挂载共享。
- 若网络不可用，进入设备会返回空列表或错误，不影响其它本地设备。

## 编译

默认构建（`NETFS=0`）包含完整中文界面但不链接网络栈；需要网络文件浏览时
显式开启：

```
make NETFS=1
```

开启 `NETFS=1` 会自动强制 `ETH=1`，并链接 `libsmb2`、`libnfs`、`ps2ip`、
`netman`。

CI 构建矩阵通过 `scripts/ci/resolve_make_args.sh` 的第六个参数控制：

```
scripts/ci/resolve_make_args.sh no-psx no-ds34 all 0 0 1
# -> ... NETFS=1
```

## 实现说明

- 客户端代码来自 libsmb2 / libnfs 的移植（`src/smb2.c`、`src/nfs.c`）。
- 文件操作路由在 `src/filer_fileops.c` 的 `gen*` 系列函数顶部，以
  `#ifdef NETFS` 隔离；`SMB:`/`nfs:` 设备映射到高位 fd 表（`NETFS_FD_BASE`）。
- 网络栈在 `src/init.c` 的 `init_netfs()` 中拉起，并与既有 ETH/SMAP 路径复用。
- 主设备列表与配置菜单入口分别在 `src/filer.c` 与 `src/config.c` 中增加。
