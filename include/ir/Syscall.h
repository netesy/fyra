#pragma once

#include <string>
#include <map>

namespace ir {

enum class SyscallId {
    None,
    Exit,
    Execve,
    Fork,
    Clone,
    Wait4,
    Kill,
    Read,
    Write,
    OpenAt,
    Close,
    LSeek,
    MMap,
    MUnmap,
    MProtect,
    Brk,
    MkDirAt,
    UnlinkAt,
    RenameAt,
    GetDents64,
    ClockGetTime,
    NanoSleep,
    RtSigAction,
    RtSigProcMask,
    RtSigReturn,
    GetRandom,
    Uname,
    GetPid,
    GetTid
};

inline std::string syscallIdToString(SyscallId id) {
    static const std::map<SyscallId, std::string> mapping = {
        {SyscallId::None, "none"},
        {SyscallId::Exit, "sys_exit"},
        {SyscallId::Execve, "sys_execve"},
        {SyscallId::Fork, "sys_fork"},
        {SyscallId::Clone, "sys_clone"},
        {SyscallId::Wait4, "sys_wait4"},
        {SyscallId::Kill, "sys_kill"},
        {SyscallId::Read, "sys_read"},
        {SyscallId::Write, "sys_write"},
        {SyscallId::OpenAt, "sys_openat"},
        {SyscallId::Close, "sys_close"},
        {SyscallId::LSeek, "sys_lseek"},
        {SyscallId::MMap, "sys_mmap"},
        {SyscallId::MUnmap, "sys_munmap"},
        {SyscallId::MProtect, "sys_mprotect"},
        {SyscallId::Brk, "sys_brk"},
        {SyscallId::MkDirAt, "sys_mkdirat"},
        {SyscallId::UnlinkAt, "sys_unlinkat"},
        {SyscallId::RenameAt, "sys_renameat"},
        {SyscallId::GetDents64, "sys_getdents64"},
        {SyscallId::ClockGetTime, "sys_clock_gettime"},
        {SyscallId::NanoSleep, "sys_nanosleep"},
        {SyscallId::RtSigAction, "sys_rt_sigaction"},
        {SyscallId::RtSigProcMask, "sys_rt_sigprocmask"},
        {SyscallId::RtSigReturn, "sys_rt_sigreturn"},
        {SyscallId::GetRandom, "sys_getrandom"},
        {SyscallId::Uname, "sys_uname"},
        {SyscallId::GetPid, "sys_getpid"},
        {SyscallId::GetTid, "sys_gettid"}
    };
    auto it = mapping.find(id);
    return (it != mapping.end()) ? it->second : "unknown";
}

inline SyscallId stringToSyscallId(const std::string& name) {
    static const std::map<std::string, SyscallId> mapping = {
        {"sys_exit", SyscallId::Exit},
        {"sys_execve", SyscallId::Execve},
        {"sys_fork", SyscallId::Fork},
        {"sys_clone", SyscallId::Clone},
        {"sys_wait4", SyscallId::Wait4},
        {"sys_kill", SyscallId::Kill},
        {"sys_read", SyscallId::Read},
        {"sys_write", SyscallId::Write},
        {"sys_openat", SyscallId::OpenAt},
        {"sys_close", SyscallId::Close},
        {"sys_lseek", SyscallId::LSeek},
        {"sys_mmap", SyscallId::MMap},
        {"sys_munmap", SyscallId::MUnmap},
        {"sys_mprotect", SyscallId::MProtect},
        {"sys_brk", SyscallId::Brk},
        {"sys_mkdirat", SyscallId::MkDirAt},
        {"sys_unlinkat", SyscallId::UnlinkAt},
        {"sys_renameat", SyscallId::RenameAt},
        {"sys_getdents64", SyscallId::GetDents64},
        {"sys_clock_gettime", SyscallId::ClockGetTime},
        {"sys_nanosleep", SyscallId::NanoSleep},
        {"sys_rt_sigaction", SyscallId::RtSigAction},
        {"sys_rt_sigprocmask", SyscallId::RtSigProcMask},
        {"sys_rt_sigreturn", SyscallId::RtSigReturn},
        {"sys_getrandom", SyscallId::GetRandom},
        {"sys_uname", SyscallId::Uname},
        {"sys_getpid", SyscallId::GetPid},
        {"sys_gettid", SyscallId::GetTid}
    };
    auto it = mapping.find(name);
    return (it != mapping.end()) ? it->second : SyscallId::None;
}

} // namespace ir
