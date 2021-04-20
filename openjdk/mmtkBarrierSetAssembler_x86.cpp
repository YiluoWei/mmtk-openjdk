/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "mmtkBarrierSet.hpp"
#include "mmtkBarrierSetAssembler_x86.hpp"
#include "interpreter/interp_masm.hpp"
#include "runtime/sharedRuntime.hpp"
#include "utilities/macros.hpp"
#include "mmtkMutator.hpp"

#define SIDE_METADATA_BASE_ADDRESS 0x160000000000L

#define __ masm->

void MMTkBarrierSetAssembler::eden_allocate(MacroAssembler* masm, Register thread, Register obj, Register var_size_in_bytes, int con_size_in_bytes, Register t1, Label& slow_case) {
  assert(obj == rax, "obj must be in rax, for cmpxchg");
  assert_different_registers(obj, var_size_in_bytes, t1);
  // printf("eden_allocate\n");
  if (!MMTK_ENABLE_ALLOCATION_FASTPATH) {
    __ jmp(slow_case);
  } else {
    // We always use the default allocator.
    // But we need to figure out which allocator we are using by querying MMTk.
    AllocatorSelector selector = get_allocator_mapping(AllocatorDefault);

    if (selector.tag == TAG_MALLOC) {
      __ jmp(slow_case);
      return;
    }

    // Only bump pointer allocator is implemented.
    if (selector.tag != TAG_BUMP_POINTER) {
      fatal("unimplemented allocator fastpath\n");
    }

    // Calculate offsets of top and end. We now assume we are using bump pointer.
    int allocator_base_offset = in_bytes(JavaThread::third_party_heap_mutator_offset())
      + in_bytes(byte_offset_of(MMTkMutatorContext, allocators))
      + in_bytes(byte_offset_of(Allocators, bump_pointer))
      + selector.index * sizeof(BumpAllocator);

    Address cursor = Address(r15_thread, allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, cursor)));
    Address limit = Address(r15_thread, allocator_base_offset + in_bytes(byte_offset_of(BumpAllocator, limit)));
    // obj = load lab.cursor
    __ movptr(obj, cursor);
    // end = obj + size
    Register end = t1;
    if (var_size_in_bytes == noreg) {
      __ lea(end, Address(obj, con_size_in_bytes));
    } else {
      __ lea(end, Address(obj, var_size_in_bytes, Address::times_1));
    }
    // slowpath if end < obj
    __ cmpptr(end, obj);
    __ jcc(Assembler::below, slow_case);
    // slowpath if end > lab.limit
    __ cmpptr(end, limit);
    __ jcc(Assembler::above, slow_case);
    // lab.cursor = end
    __ movptr(cursor, end);

    Register tmp3 = rscratch2;
    Register tmp2 = rscratch1;
    Register tmp4 = t1;
    assert_different_registers(obj, tmp2, tmp3);

    // tmp2 = load-byte (SIDE_METADATA_BASE_ADDRESS + (obj >> 6));
    __ movptr(tmp3, obj);
    __ shrptr(tmp3, 6);
    __ movptr(tmp2, SIDE_METADATA_BASE_ADDRESS);
    __ movb(tmp2, Address(tmp2, tmp3));
    // // tmp3 = 1 << ((obj >> 3) & 7)
    __ movptr(tmp4, obj);
    __ shrptr(tmp4, 3);
    __ andptr(tmp4, 7);
    __ movptr(tmp3, 1);
    __ shlptr(tmp3, 1);  // FIXME: should do shlptr(tmp3, tmp4)
    // // tmp2 = tmp2 | tmp3
    __ orptr(tmp2, tmp3);

    // // store-byte tmp2 (SIDE_METADATA_BASE_ADDRESS + (obj >> 6))
    __ movptr(tmp3, obj);
    __ shrptr(tmp3, 6);
    __ movptr(tmp4, SIDE_METADATA_BASE_ADDRESS);
    __ movb(Address(tmp2, tmp3), tmp2);  // FIXME: should use CAS


    // BarrierSetAssembler::incr_allocated_bytes
    if (var_size_in_bytes->is_valid()) {
      __ addq(Address(r15_thread, in_bytes(JavaThread::allocated_bytes_offset())), var_size_in_bytes);
    } else {
      __ addq(Address(r15_thread, in_bytes(JavaThread::allocated_bytes_offset())), con_size_in_bytes);
    }
  }
}

