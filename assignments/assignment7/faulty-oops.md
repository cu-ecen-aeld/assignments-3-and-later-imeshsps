# Debugging Kernel NULL Pointer Dereference in a Custom Module

## Overview

While experimenting with a custom kernel module (`faulty.ko`) on a Yocto Project QEMU setup, we observed a kernel crash when writing to the device:

```bash
root@qemuarm64:~# echo "hello" >> /dev/faulty
root@qemuarm64:~# echo "hello" >> /dev/faulty 
[ 28.934187] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000 [ 28.935306] Mem abort info: [ 28.935460] ESR = 0x0000000096000045 [ 28.935637] EC = 0x25: DABT (current EL), IL = 32 bits [ 28.936752] SET = 0, FnV = 0 [ 28.936941] EA = 0, S1PTW = 0 [ 28.937095] FSC = 0x05: level 1 translation fault [ 28.937286] Data abort info: [ 28.937400] ISV = 0, ISS = 0x00000045 [ 28.937534] CM = 0, WnR = 1 [ 28.937798] user pgtable: 4k pages, 39-bit VAs, pgdp=0000000042018000 [ 28.938030] [0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000 [ 28.938714] Internal error: Oops: 0000000096000045 [#1] PREEMPT SMP [ 28.939622] Modules linked in: scull(O) faulty(O) hello(O) [ 28.940405] CPU: 1 PID: 304 Comm: sh Tainted: G O 5.15.186-yocto-standard #1 [ 28.940697] Hardware name: linux,dummy-virt (DT) [ 28.941111] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--) [ 28.941404] pc : faulty_write+0x18/0x20 [faulty] [ 28.942219] lr : vfs_write+0xf8/0x2a0 [ 28.942384] sp : ffffffc0096dbd80 [ 28.942539] x29: ffffffc0096dbd80 x28: ffffff8002060000 x27: 0000000000000000 [ 28.943278] x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000 [ 28.943553] x23: 0000000000000000 x22: ffffffc0096dbdc0 x21: 00000055589e6cb0 [ 28.943658] x20: ffffff80037c8100 x19: 0000000000000006 x18: 0000000000000000 [ 28.943760] x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000 [ 28.943863] x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000 [ 28.943965] x11: 0000000000000000 x10: 0000000000000000 x9 : ffffffc008271ad8 [ 28.944086] x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000 [ 28.944190] x5 : 0000000000000001 x4 : ffffffc000b95000 x3 : ffffffc0096dbdc0 [ 28.944294] x2 : 0000000000000006 x1 : 0000000000000000 x0 : 0000000000000000 [ 28.944485] Call trace: [ 28.944549] faulty_write+0x18/0x20 [faulty] [ 28.944640] ksys_write+0x74/0x110 [ 28.944691] __arm64_sys_write+0x24/0x30 [ 28.944747] invoke_syscall+0x5c/0x130 [ 28.944804] el0_svc_common.constprop.0+0x4c/0x100 [ 28.944870] do_el0_svc+0x4c/0xc0 [ 28.944921] el0_svc+0x28/0x80 [ 28.944967] el0t_64_sync_handler+0xa4/0x130 [ 28.945028] el0t_64_sync+0x1a0/0x1a4 [ 28.945161] Code: d2800001 d2800000 d503233f d50323bf (b900003f) [ 28.945402] ---[ end trace 312c2601fed071d3 ]--- Segmentation fault
```

This occurs immediately when the `write()` system call from userspace triggers the `faulty_write` function in the kernel module.

---

## What Happened

1. The device node `/dev/faulty` exists, and the module is loaded.
2. Writing to the device calls the `faulty_write()` function in the kernel module.
3. The kernel log shows a **NULL pointer dereference** at virtual address `0x0`.
4. The crash trace (`Call trace`) points to `faulty_write+0x18/0x20 [faulty]`.
5. The CPU state (`x0 = 0x0`) indicates that a pointer used inside `faulty_write` was **NULL**, causing the kernel to abort.

**Key points:**

- Kernel crashes are **fatal** and can panic QEMU.
- This is **not a userspace issue**; `echo` just triggers the kernel code.
- `strace` or other userspace tools **cannot debug this**, as the problem is inside the kernel.

---

## How to Debug Kernel NULL Pointer Dereference

### 1. Use `printk()` Logging

Add logging inside `faulty_write` to check pointer validity:

```c
ssize_t faulty_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
    printk(KERN_INFO "faulty_write called: buf=%p count=%zu\n", buf, count);
    
    if (!buf) {
        printk(KERN_ERR "NULL pointer detected!\n");
        return -EFAULT;
    }

    // rest of your code...
}
```

**Then rebuild and reload the module**:

```bash
insmod faulty.ko
echo "hello" > /dev/faulty
dmesg -w  # Watch logs live
```

---

### 2. Use `dmesg` for Live Logs

```bash
root@qemuarm64:~# dmesg -w
```

This will show kernel log messages in real-time as you interact with your module.

---

### 3. Use KGDB (Kernel GDB)

- Set up QEMU with kernel debug symbols and KGDB.
- Connect GDB from your host machine to step through `faulty_write` live.
- Allows inspection of all kernel memory and variables at crash time.

---

### 4. Use KASAN (Kernel Address Sanitizer)

- Compile the kernel with `CONFIG_KASAN=y`.
- KASAN detects invalid memory accesses and NULL dereferences **without crashing the whole system**.
- Provides detailed stack traces and memory info.

---

## Recommendations

- Always check pointers before dereferencing:

```c
if (!ptr)
    return -EFAULT;
```

- Start with `printk()` debugging — fastest and simplest.
- Use KGDB for complex pointer tracing.
- Avoid relying on userspace tools like `strace` for kernel-level issues.

---

