#!/bin/sh
set -eu

GKI_ROOT=$(pwd)

display_usage() {
    echo "Usage: $0 [--cleanup | <commit-or-tag>]"
    echo "  --cleanup:              Cleans up previous modifications made by the script."
    echo "  <commit-or-tag>:        Sets up or updates the KernelSU-Next to specified tag or commit."
    echo "  -h, --help:             Displays this usage information."
    echo "  (no args):              Sets up or updates the KernelSU-Next environment to the latest tagged version."
}

initialize_variables() {
    if test -d "$GKI_ROOT/common/drivers"; then
         DRIVER_DIR="$GKI_ROOT/common/drivers"
    elif test -d "$GKI_ROOT/drivers"; then
         DRIVER_DIR="$GKI_ROOT/drivers"
    else
         echo '[ERROR] "drivers/" directory not found.'
         exit 127
    fi

    DRIVER_MAKEFILE=$DRIVER_DIR/Makefile
    DRIVER_KCONFIG=$DRIVER_DIR/Kconfig
}

# Reverts modifications made by this script
perform_cleanup() {
    echo "[+] 清理集成..."
    [ -L "$DRIVER_DIR/kernelsu" ] && rm "$DRIVER_DIR/kernelsu" && echo "[-] 还原修改的符号链接..."
    grep -q "kernelsu" "$DRIVER_MAKEFILE" && sed -i '/kernelsu/d' "$DRIVER_MAKEFILE" && echo "[-] 还原修改的 Makefile..."
    grep -q "drivers/kernelsu/Kconfig" "$DRIVER_KCONFIG" && sed -i '/drivers\/kernelsu\/Kconfig/d' "$DRIVER_KCONFIG" && echo "[-] 还原修改的 Kconfig..."
    if [ -d "$GKI_ROOT/KernelSU-Next" ]; then
        rm -rf "$GKI_ROOT/KernelSU-Next" && echo "[-] KSU已成功移除！"
    fi
}

# Sets up or update KernelSU-Next environment
setup_kernelsu() {
    echo "[+] 正在集成KSU全管理器支持..."
    test -d "$GKI_ROOT/KernelSU-Next" || git clone https://github.com/Winkmoon/KernelSU-Next && echo "[+] 当前仓库由 然 维护..."
    cd "$GKI_ROOT/KernelSU-Next"
    git stash && echo "[-] 暂存当前更改..."
    if [ "$(git status | grep -Po 'v\d+(\.\d+)*' | head -n1)" ]; then
        git checkout next && echo "[-] 切换分支..."
    fi
    git pull && echo "[+] 更新仓库..."
    if [ -z "${1-}" ]; then
        git checkout "$(git describe --abbrev=0 --tags)" && echo "[-] 预览最新标签..."
    else
        git checkout "$1" && echo "[-] Checked out $1." || echo "[-] 预览默认分支..."
    fi
    cd "$DRIVER_DIR"
    ln -sf "$(realpath --relative-to="$DRIVER_DIR" "$GKI_ROOT/KernelSU-Next/kernel")" "kernelsu" && echo "[+] 创建符号链接..."

    # Add entries in Makefile and Kconfig if not already existing
    grep -q "kernelsu" "$DRIVER_MAKEFILE" || printf "\nobj-\$(CONFIG_KSU) += kernelsu/\n" >> "$DRIVER_MAKEFILE" && echo "[+] 修改 Makefile..."
    grep -q "source \"drivers/kernelsu/Kconfig\"" "$DRIVER_KCONFIG" || sed -i "/endmenu/i\source \"drivers/kernelsu/Kconfig\"" "$DRIVER_KCONFIG" && echo "[+] 修改 Kconfig..."
    echo '[+] 完成！'
}

# Process command-line arguments
if [ "$#" -eq 0 ]; then
    initialize_variables
    setup_kernelsu
elif [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    display_usage
elif [ "$1" = "--cleanup" ]; then
    initialize_variables
    perform_cleanup
else
    initialize_variables
    setup_kernelsu "$@"
fi
