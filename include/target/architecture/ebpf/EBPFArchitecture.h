#pragma once

#include "target/architecture/bpf/BPFArchitecture.h"

namespace target {

// Compatibility spelling only.  Linux names the modern instruction set eBPF,
// while ELF and the target triple registry use the ABI names BPF/EM_BPF.  There
// is one implementation, BPFArchitecture; this alias does not define a second
// backend.
using EBPFArchitecture = BPFArchitecture;

}
