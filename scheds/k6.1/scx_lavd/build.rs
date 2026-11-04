// Copyright (c) Changwoo Min <changwoo@igalia.com>
//
// This software may be used and distributed according to the terms of the
// GNU General Public License version 2.

fn main() {
    let manifest_dir = std::env::var_os("CARGO_MANIFEST_DIR").unwrap();
    let include_dir = std::path::PathBuf::from(manifest_dir)
        .parent()
        .unwrap()
        .join("include");
    unsafe {
        std::env::set_var("SCX_FCP_ANDROID_61", "1");
        let mut pre_incl = std::env::var("BPF_EXTRA_CFLAGS_PRE_INCL").unwrap_or_default();
        if !pre_incl.is_empty() {
            pre_incl.push(' ');
        }
        pre_incl.push_str(&format!("-I{} -DSCX_FCP_ANDROID_61", include_dir.display()));
        std::env::set_var("BPF_EXTRA_CFLAGS_PRE_INCL", pre_incl);
    }
    println!("cargo:rustc-check-cfg=cfg(scx_android_61)");
    let android_61 = std::env::var_os("SCX_FCP_ANDROID_61").is_some();
    if android_61 {
        println!("cargo:rustc-cfg=scx_android_61");
        let mut bpf_cflags =
            std::env::var("BPF_EXTRA_CFLAGS_POST_INCL").unwrap_or_else(|_| "-mcpu=v3".to_owned());
        bpf_cflags.push_str(" -DSCX_FCP_ANDROID_61");
        unsafe {
            std::env::set_var("BPF_EXTRA_CFLAGS_POST_INCL", bpf_cflags);
        }
    }

    let mut builder = scx_cargo::BpfBuilder::new().unwrap();
    builder
        .enable_intf("src/bpf/intf.h", "bpf_intf.rs")
        .enable_skel("src/bpf/main.bpf.c", "bpf");
    if android_61 {
    } else {
        builder
            .add_source("src/bpf/balance.bpf.c")
            .add_source("src/bpf/idle.bpf.c")
            .add_source("src/bpf/introspec.bpf.c")
            .add_source("src/bpf/lat_cri.bpf.c")
            .add_source("src/bpf/lock.bpf.c")
            .add_source("src/bpf/power.bpf.c")
            .add_source("src/bpf/preempt.bpf.c")
            .add_source("src/bpf/sys_stat.bpf.c")
            .add_source("src/bpf/util.bpf.c")
            .add_source("src/bpf/lib/arena.bpf.c")
            .add_source("src/bpf/lib/common.bpf.c")
            .add_source("src/bpf/lib/atq.bpf.c")
            .add_source("src/bpf/lib/bitmap.bpf.c")
            .add_source("src/bpf/lib/cgroup_bw.bpf.c")
            .add_source("src/bpf/lib/cpumask.bpf.c")
            .add_source("src/bpf/lib/rbtree.bpf.c")
            .add_source("src/bpf/lib/minheap.bpf.c")
            .add_source("src/bpf/lib/sdt_alloc.bpf.c")
            .add_source("src/bpf/lib/sdt_task.bpf.c")
            .add_source("src/bpf/lib/topology.bpf.c")
            .add_source("src/bpf/lib/ravg.bpf.c");
    }
    builder.compile_link_gen().unwrap();
}
