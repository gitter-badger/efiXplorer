/*
 *        __ ___   __      _
 *       / _(_) \ / /     | |
 *   ___| |_ _ \ V / _ __ | | ___  _ __ ___ _ __
 *  / _ \  _| | > < | '_ \| |/ _ \| '__/ _ \ '__|
 * |  __/ | | |/ . \| |_) | | (_) | | |  __/ |
 *  \___|_| |_/_/ \_\ .__/|_|\___/|_|  \___|_|
 *                  | |
 *                  |_|
 *
 * efiXplorer
 * Copyright (C) 2020  Binarly
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * efiSmmUtils.cpp
 *
 */

#define _CRT_SECURE_NO_WARNINGS

#include "efiSmmUtils.h"

static const char plugin_name[] = "efiXplorer";

//--------------------------------------------------------------------------
// Find and mark gSmst global variable
vector<ea_t> findSmst() {
    vector<ea_t> resAddrs;
    efiGuid efiSmmSwDispatch2ProtocolGuid = {
        0x18a3c6dc,
        0x5eea,
        0x48c8,
        {0xa1, 0xc1, 0xb5, 0x33, 0x89, 0xf9, 0x89, 0x99}};
    efiGuid efiSmmSwDispatchProtocolGuid = {
        0xe541b773,
        0xdd11,
        0x420c,
        {0xb0, 0x26, 0xdf, 0x99, 0x36, 0x53, 0xf8, 0xbf}};
    ea_t efiSmmSwDispatchProtocolGuidAddr = 0;
    string segName = ".data";
    segment_t *seg_info = get_segm_by_name(segName.c_str());
    if (seg_info == nullptr) {
        return resAddrs;
    }
    ea_t ea = seg_info->start_ea;
    while (ea != BADADDR && ea <= seg_info->end_ea - 15) {
        if (get_wide_dword(ea) == efiSmmSwDispatchProtocolGuid.data1 ||
            get_wide_dword(ea) == efiSmmSwDispatch2ProtocolGuid.data1) {
            efiSmmSwDispatchProtocolGuidAddr = ea;
            break;
        }
        ea += 1;
    }
    if (!efiSmmSwDispatchProtocolGuidAddr) {
        return resAddrs;
    }
    vector<ea_t> efiSmmSwDispatchProtocolGuidXrefs =
        getXrefs(efiSmmSwDispatchProtocolGuidAddr);
    for (vector<ea_t>::iterator guidXref =
             efiSmmSwDispatchProtocolGuidXrefs.begin();
         guidXref != efiSmmSwDispatchProtocolGuidXrefs.end(); ++guidXref) {
        ea_t resAddr = 0;
        ea_t curAddr = prev_head(*guidXref, 0);
        insn_t insn;
        /* 4 instructions below */
        for (int i = 0; i < 4; i++) {
            decode_insn(&insn, curAddr);
            /* check if insn like 'mov rax, cs:<gSmst>' */
            if (insn.itype == NN_mov && insn.ops[0].type == o_reg &&
                insn.ops[0].reg == REG_RAX && insn.ops[1].type == o_mem) {
                DEBUG_MSG("[%s] found gSmst at 0x%016X, address = 0x%016X\n",
                          plugin_name, curAddr, insn.ops[1].addr);
                resAddr = insn.ops[1].addr;
                char hexAddr[16] = {};
                sprintf(hexAddr, "%llX", static_cast<uint64_t>(resAddr));
                set_cmt(curAddr, "EFI_SMM_SYSTEM_TABLE2 *gSmst;", true);
                string name = "gSmst_" + static_cast<string>(hexAddr);
                setSmstTypeAndName(resAddr, name);
                resAddrs.push_back(resAddr);
                break;
            }
            curAddr = prev_head(curAddr, 0);
        }
    }
    return resAddrs;
}

//--------------------------------------------------------------------------
// Find SwSmiHandler function inside SMM drivers
//  * find EFI_SMM_SW_DISPATCH(2)_PROTOCOL_GUID
//  * get EFI_SMM_SW_DISPATCH(2)_PROTOCOL_GUID xref address
//  * this address will be inside RegSwSmi function
//  * find SmiHandler by pattern (instructions may be out of order)
//        lea     r9, ...
//        lea     r8, ...
//        lea     rdx, <func>
//        call    qword ptr [...]
func_t *findSmiHandlerSmmSwDispatch() {
    DEBUG_MSG("[%s] SwSmiHandler function finding (using "
              "EFI_SMM_SW_DISPATCH(2)_PROTOCOL_GUID)\n",
              plugin_name);
    efiGuid efiSmmSwDispatch2ProtocolGuid = {
        0x18a3c6dc,
        0x5eea,
        0x48c8,
        {0xa1, 0xc1, 0xb5, 0x33, 0x89, 0xf9, 0x89, 0x99}};
    efiGuid efiSmmSwDispatchProtocolGuid = {
        0xe541b773,
        0xdd11,
        0x420c,
        {0xb0, 0x26, 0xdf, 0x99, 0x36, 0x53, 0xf8, 0xbf}};
    ea_t efiSmmSwDispatchProtocolGuidAddr = 0;
    string segName = ".data";
    segment_t *seg_info = get_segm_by_name(segName.c_str());
    if (seg_info == nullptr) {
        DEBUG_MSG("[%s] can't find a %s segment\n", plugin_name,
                  segName.c_str());
        return nullptr;
    }
    ea_t ea = seg_info->start_ea;
    while (ea != BADADDR && ea <= seg_info->end_ea - 15) {
        if (get_wide_dword(ea) == efiSmmSwDispatchProtocolGuid.data1 ||
            get_wide_dword(ea) == efiSmmSwDispatch2ProtocolGuid.data1) {
            efiSmmSwDispatchProtocolGuidAddr = ea;
            break;
        }
        ea += 1;
    }
    if (!efiSmmSwDispatchProtocolGuidAddr) {
        DEBUG_MSG(
            "[%s] can't find a EFI_SMM_SW_DISPATCH(2)_PROTOCOL_GUID guid\n",
            plugin_name);
        return nullptr;
    }
    DEBUG_MSG("[%s] EFI_SMM_SW_DISPATCH(2)_PROTOCOL_GUID address: 0x%016X\n",
              plugin_name, efiSmmSwDispatchProtocolGuidAddr);
    vector<ea_t> efiSmmSwDispatchProtocolGuidXrefs =
        getXrefs(efiSmmSwDispatchProtocolGuidAddr);
    for (vector<ea_t>::iterator guidXref =
             efiSmmSwDispatchProtocolGuidXrefs.begin();
         guidXref != efiSmmSwDispatchProtocolGuidXrefs.end(); ++guidXref) {
        DEBUG_MSG(
            "[%s] EFI_SMM_SW_DISPATCH(2)_PROTOCOL_GUID xref address: 0x%016X\n",
            plugin_name, *guidXref);
        /* get RegSwSmi function */
        func_t *regSmi = get_func(*guidXref);
        ea_t start = 0;
        insn_t insn;
        if (regSmi == nullptr) {
            DEBUG_MSG(
                "[%s] can't get RegSwSmi function, will try to create it\n",
                plugin_name)
            /* try to create function */
            ea = *guidXref;
            /* find function start */
            for (int i = 0; i < 100; i++) {
                /* find 'retn' insn */
                ea = prev_head(ea, 0);
                decode_insn(&insn, ea);
                if (insn.itype == NN_retn) {
                    start = next_head(ea, BADADDR);
                    break;
                }
            }
            /* create function */
            add_func(start);
            regSmi = get_func(*guidXref);
            if (regSmi == nullptr) {
                continue;
            }
        }
        /* find (SwDispath->Register)(SwDispath, SwSmiHandler, &SwSmiNum, Data)
         */
        for (ea_t ea = regSmi->start_ea; ea <= regSmi->end_ea;
             ea = next_head(ea, BADADDR)) {
            decode_insn(&insn, ea);
            if (insn.itype == NN_callni) {
                /* find 'lea r9' */
                bool success = false;
                ea_t addr = prev_head(ea, 0);
                for (int i = 0; i < 12; i++) {
                    decode_insn(&insn, addr);
                    if (insn.itype == NN_lea && insn.ops[0].reg == REG_R9 &&
                        insn.ops[1].type == o_displ) {
                        success = true;
                        break;
                    }
                    addr = prev_head(addr, 0);
                }
                if (!success)
                    continue;
                /* find 'lea r8' */
                success = false;
                addr = prev_head(ea, 0);
                for (int i = 0; i < 12; i++) {
                    decode_insn(&insn, addr);
                    if (insn.itype == NN_lea && insn.ops[0].reg == REG_R8 &&
                        insn.ops[1].type == o_displ) {
                        success = true;
                        break;
                    }
                    addr = prev_head(addr, 0);
                }
                if (!success)
                    continue;
                /* find 'lea rdx' */
                success = false;
                addr = prev_head(ea, 0);
                for (int i = 0; i < 12; i++) {
                    decode_insn(&insn, addr);
                    if (insn.itype == NN_lea && insn.ops[0].reg == REG_RDX &&
                        insn.ops[1].type == o_mem) {
                        success = true;
                        break;
                    }
                    addr = prev_head(addr, 0);
                }
                if (!success)
                    continue;
                ea_t smiHandlerAddr = insn.ops[1].addr;
                func_t *smiHandler = get_func(smiHandlerAddr);
                if (smiHandler == nullptr) {
                    DEBUG_MSG(
                        "[%s] can't get SwSmiHandler function, will try to "
                        "create it\n",
                        plugin_name);
                    /* create function */
                    add_func(smiHandlerAddr);
                    smiHandler = get_func(smiHandlerAddr);
                }
                if (smiHandler == nullptr) {
                    continue;
                }
                /* make name for SwSmiHandler function */
                char hexAddr[16] = {0};
                sprintf(hexAddr, "%llX",
                        static_cast<uint64_t>(smiHandler->start_ea));
                string name = "SwSmiHandler_" + static_cast<string>(hexAddr);
                set_name(smiHandler->start_ea, name.c_str(), SN_CHECK);
                return smiHandler;
            }
        }
    }
    return nullptr;
}
