/***************************************************************************
 *   Copyright (C) 2007-2010 by Sindre Aamås                               *
 *   aamas@stud.ntnu.no                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 2 as     *
 *   published by the Free Software Foundation.                            *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License version 2 for more details.                *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   version 2 along with this program; if not, write to the               *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "cartridge.h"
#include "../savestate.h"
#include <cstring>
#include <fstream>

#include "../../libretro/libretro.h"
extern retro_log_printf_t log_cb;

namespace gambatte
{

   Cartridge::Cartridge()
      : rombank_(1),
      rambank_(0),
      enableRam_(false),
      rambankMode_(false),
      multi64rom_(false)
   {
   }

   enum Cartridgetype { PLAIN, MBC1, MBC2, MBC3, MBC5 };

   static inline unsigned rambanks(MemPtrs const &memptrs)
   {
      return (memptrs.rambankdataend() - memptrs.rambankdata()) / 0x2000;
   }

   static inline unsigned rombanks(MemPtrs const &memptrs)
   {
      return (memptrs.romdataend()     - memptrs.romdata()    ) / 0x4000;
   }

   static unsigned adjustedRombank(unsigned bank, const Cartridgetype romtype)
   {
      if ((romtype == MBC1 && !(bank & 0x1F)) || (romtype == MBC5 && !bank))
         ++bank;

      return bank;
   }

   static bool hasRtc(unsigned headerByte0x147)
   {
      switch (headerByte0x147)
      {
         case 0x0F:
         case 0x10:
            return true;
         default:
            return false;
      }
   }

   void Cartridge::setStatePtrs(SaveState &state)
   {
      state.mem.sram.set(memptrs_.rambankdata(), memptrs_.rambankdataend() - memptrs_.rambankdata());
      state.mem.wram.set(memptrs_.wramdata(0), memptrs_.wramdataend() - memptrs_.wramdata(0));
   }

   void Cartridge::saveState(SaveState &state) const
   {
      state.mem.rombank = rombank_;
      state.mem.rambank = rambank_;
      state.mem.enableRam = enableRam_;
      state.mem.rambankMode = rambankMode_;

      rtc_.saveState(state);
   }

   static Cartridgetype cartridgeType(const unsigned headerByte0x147)
   {
      static const unsigned char typeLut[] = {
         /* [0xFF] = */ MBC1,
         /* [0x00] = */ PLAIN,
         /* [0x01] = */ MBC1,
         /* [0x02] = */ MBC1,
         /* [0x03] = */ MBC1,
         /* [0x04] = */ 0,
         /* [0x05] = */ MBC2,
         /* [0x06] = */ MBC2,
         /* [0x07] = */ 0,
         /* [0x08] = */ PLAIN,
         /* [0x09] = */ PLAIN,
         /* [0x0A] = */ 0,
         /* [0x0B] = */ 0,
         /* [0x0C] = */ 0,
         /* [0x0D] = */ 0,
         /* [0x0E] = */ 0,
         /* [0x0F] = */ MBC3,
         /* [0x10] = */ MBC3,
         /* [0x11] = */ MBC3,
         /* [0x12] = */ MBC3,
         /* [0x13] = */ MBC3,
         /* [0x14] = */ 0,
         /* [0x15] = */ 0,
         /* [0x16] = */ 0,
         /* [0x17] = */ 0,
         /* [0x18] = */ 0,
         /* [0x19] = */ MBC5,
         /* [0x1A] = */ MBC5,
         /* [0x1B] = */ MBC5,
         /* [0x1C] = */ MBC5,
         /* [0x1D] = */ MBC5,
         /* [0x1E] = */ MBC5
      };

      return static_cast<Cartridgetype>(typeLut[(headerByte0x147 + 1) & 0x1F]);
   }

   static unsigned toMulti64Rombank(const unsigned rombank)
   {
      return (rombank >> 1 & 0x30) | (rombank & 0xF);
   }

   void Cartridge::loadState(const SaveState &state)
   {
      rtc_.loadState(state, hasRtc(memptrs_.romdata()[0x147]) ? state.mem.enableRam : false);

      rombank_ = state.mem.rombank;
      rambank_ = state.mem.rambank;
      enableRam_ = state.mem.enableRam;
      rambankMode_ = state.mem.rambankMode;
      memptrs_.setRambank(enableRam_, rtc_.getActive(), rambank_);

      if (rambankMode_ && multi64rom_)
      {
         const unsigned rb = toMulti64Rombank(rombank_);
         memptrs_.setRombank0(rb & 0x30);
         memptrs_.setRombank(adjustedRombank(rb, cartridgeType(memptrs_.romdata()[0x147])));
      }
      else
      {
         memptrs_.setRombank0(0);
         memptrs_.setRombank(adjustedRombank(rombank_ & (rombanks(memptrs_) - 1), cartridgeType(memptrs_.romdata()[0x147])));
      }
   }

   void Cartridge::mbcWrite(const unsigned P, const unsigned data)
   {
      const Cartridgetype romtype = cartridgeType(memptrs_.romdata()[0x147]);   

      switch (P >> 12 & 0x7)
      {
         case 0x0:
         case 0x1: //Most MBCs write 0x?A to addresses lower than 0x2000 to enable ram.
            if (romtype == MBC2 && (P & 0x0100))
               break;

            enableRam_ = (data & 0x0F) == 0xA;

            if (hasRtc(memptrs_.romdata()[0x147]))
               rtc_.setEnabled(enableRam_);

            memptrs_.setRambank(enableRam_, rtc_.getActive(), rambank_);
            break;
            //MBC1 writes ???n nnnn to address area 0x2000-0x3FFF, ???n nnnn makes up the lower digits to determine which rombank to load.
            //MBC3 writes ?nnn nnnn to address area 0x2000-0x3FFF, ?nnn nnnn makes up the lower digits to determine which rombank to load.
            //MBC5 writes nnnn nnnn to address area 0x2000-0x2FFF, nnnn nnnn makes up the lower digits to determine which rombank to load.
            //MBC5 writes bit8 of the number that determines which rombank to load to address 0x3000-0x3FFF.
         case 0x2:
            switch (romtype)
            {
               case PLAIN:
                  return;
               case MBC5:
                  rombank_ = (rombank_ & 0x100) | data;
                  memptrs_.setRombank(adjustedRombank(rombank_ & (rombanks(memptrs_) - 1), romtype));
                  return;
               default:
                  break; //Only supposed to break one level.
            }
         case 0x3:
            switch (romtype)
            {
               case MBC1:
                  rombank_ = rambankMode_ && !multi64rom_ ? data & 0x1F : (rombank_ & 0x60) | (data & 0x1F);

                  if (rambankMode_ && multi64rom_)
                  {
                     memptrs_.setRombank(adjustedRombank(toMulti64Rombank(rombank_), romtype));
                     return;
                  }
                  break;
               case MBC2:
                  if (P & 0x0100)
                  {
                     rombank_ = data & 0x0F;
                     break;
                  }

                  return;
               case MBC3:
                  rombank_ = data & 0x7F;
                  break;
               case MBC5:
                  rombank_ = (data & 0x1) << 8 | (rombank_ & 0xFF);
                  break;
               default:
                  return;
            }

            memptrs_.setRombank(adjustedRombank(rombank_ & (rombanks(memptrs_) - 1), romtype));
            break;
            //MBC1 writes ???? ??nn to area 0x4000-0x5FFF either to determine rambank to load, or upper 2 bits of the rombank number to load, depending on rom-mode.
            //MBC3 writes ???? ??nn to area 0x4000-0x5FFF to determine rambank to load
            //MBC5 writes ???? nnnn to area 0x4000-0x5FFF to determine rambank to load
         case 0x4:
         case 0x5:
            switch (romtype)
            {
               case MBC1:
                  if (rambankMode_)
                  {
                     if (multi64rom_)
                     {
                        rombank_ = (data & 0x03) << 5 | (rombank_ & 0x1F);

                        const unsigned rb = toMulti64Rombank(rombank_);
                        memptrs_.setRombank0(rb & 0x30);
                        memptrs_.setRombank(adjustedRombank(rb, romtype));
                        return;
                     }

                     rambank_ = data & 0x03;
                     break;
                  }

                  rombank_ = (data & 0x03) << 5 | (rombank_ & 0x1F);
                  memptrs_.setRombank(adjustedRombank(rombank_ & (rombanks(memptrs_) - 1), romtype));
                  return;
               case MBC3:
                  if (hasRtc(memptrs_.romdata()[0x147]))
                     rtc_.swapActive(data);

                  rambank_ = data & 0x03;
                  break;
               case MBC5:
                  rambank_ = data & 0x0F;
                  break;
               default:
                  return;
            }

            memptrs_.setRambank(enableRam_, rtc_.getActive(), rambank_ & (rambanks(memptrs_) - 1));
            break;
            //MBC1: If ???? ???1 is written to area 0x6000-0x7FFFF rom will be set to rambank mode.
         case 0x6:
         case 0x7:
            switch (romtype)
            {
               case MBC1:
                  rambankMode_ = data & 0x01;

                  if (multi64rom_)
                  {
                     if (rambankMode_)
                     {
                        const unsigned rb = toMulti64Rombank(rombank_);
                        memptrs_.setRombank0(rb & 0x30);
                        memptrs_.setRombank(adjustedRombank(rb, romtype));
                     }
                     else
                     {
                        memptrs_.setRombank0(0);
                        memptrs_.setRombank(adjustedRombank(rombank_ & (rombanks(memptrs_) - 1), romtype));
                     }
                  }
                  break;
               case MBC3:
                  rtc_.latch(data);
                  break;
               default:
                  break;
            }

            break;
      }
   }

   static void enforce8bit(unsigned char *data, unsigned long sz)
   {
      if (static_cast<unsigned char>(0x100))
         while (sz--)
            *data++ &= 0xFF;
   }

   static unsigned pow2ceil(unsigned n)
   {
      --n;
      n |= n >> 1;
      n |= n >> 2;
      n |= n >> 4;
      n |= n >> 8;
      ++n;

      return n;
   }

   bool Cartridge::loadROM(const void *romdata, unsigned romsize, const bool forceDmg, const bool multiCartCompat)
   {
      File rom(romdata, romsize);
      return loadROM(rom, forceDmg, multiCartCompat);
   }

   bool Cartridge::loadROM(File &rom, const bool forceDmg, const bool multiCartCompat)
   {

      if (rom.size() < 0x4000) return 1;

      unsigned rambanks = 1;
      unsigned rombanks = 2;
      bool cgb = false;

      {
         unsigned char header[0x150];
         rom.read(reinterpret_cast<char*>(header), sizeof(header));

         switch (header[0x0147])
         {
            case 0x00: log_cb(RETRO_LOG_INFO, "Plain ROM loaded.\n"); break;
            case 0x01: log_cb(RETRO_LOG_INFO, "MBC1 ROM loaded.\n"); break;
            case 0x02: log_cb(RETRO_LOG_INFO, "MBC1 ROM+RAM loaded.\n"); break;
            case 0x03: log_cb(RETRO_LOG_INFO, "MBC1 ROM+RAM+BATTERY loaded.\n"); break;
            case 0x05: log_cb(RETRO_LOG_INFO, "MBC2 ROM loaded.\n"); break;
            case 0x06: log_cb(RETRO_LOG_INFO, "MBC2 ROM+BATTERY loaded.\n"); break;
            case 0x08: log_cb(RETRO_LOG_INFO, "Plain ROM with additional RAM loaded.\n"); break;
            case 0x09: log_cb(RETRO_LOG_INFO, "Plain ROM with additional RAM and Battery loaded.\n"); break;
            case 0x0B: log_cb(RETRO_LOG_ERROR, "MM01 ROM not supported.\n"); return 1;
            case 0x0C: log_cb(RETRO_LOG_ERROR, "MM01 ROM not supported.\n"); return 1;
            case 0x0D: log_cb(RETRO_LOG_ERROR, "MM01 ROM not supported.\n"); return 1;
            case 0x0F: log_cb(RETRO_LOG_INFO, "MBC3 ROM+TIMER+BATTERY loaded.\n"); break;
            case 0x10: log_cb(RETRO_LOG_INFO, "MBC3 ROM+TIMER+RAM+BATTERY loaded.\n"); break;
            case 0x11: log_cb(RETRO_LOG_INFO, "MBC3 ROM loaded.\n"); break;
            case 0x12: log_cb(RETRO_LOG_INFO, "MBC3 ROM+RAM loaded.\n"); break;
            case 0x13: log_cb(RETRO_LOG_INFO, "MBC3 ROM+RAM+BATTERY loaded.\n"); break;
            case 0x15: log_cb(RETRO_LOG_ERROR, "MBC4 ROM not supported.\n"); return 1;
            case 0x16: log_cb(RETRO_LOG_ERROR, "MBC4 ROM not supported.\n"); return 1;
            case 0x17: log_cb(RETRO_LOG_ERROR, "MBC4 ROM not supported.\n"); return 1;
            case 0x19: log_cb(RETRO_LOG_INFO, "MBC5 ROM loaded.\n"); break;
            case 0x1A: log_cb(RETRO_LOG_INFO, "MBC5 ROM+RAM loaded.\n"); break;
            case 0x1B: log_cb(RETRO_LOG_INFO, "MBC5 ROM+RAM+BATTERY loaded.\n"); break;
            case 0x1C: log_cb(RETRO_LOG_INFO, "MBC5+RUMBLE ROM not supported.\n"); break;
            case 0x1D: log_cb(RETRO_LOG_INFO, "MBC5+RUMBLE+RAM ROM not suported.\n"); break;
            case 0x1E: log_cb(RETRO_LOG_INFO, "MBC5+RUMBLE+RAM+BATTERY ROM not supported.\n"); break;
            case 0xFC: log_cb(RETRO_LOG_ERROR, "Pocket Camera ROM not supported.\n"); return 1;
            case 0xFD: log_cb(RETRO_LOG_ERROR, "Bandai TAMA5 ROM not supported.\n"); return 1;
            case 0xFE: log_cb(RETRO_LOG_ERROR, "HuC3 ROM not supported.\n"); return 1;
            case 0xFF: log_cb(RETRO_LOG_ERROR, "HuC1 ROM not supported.\n"); return 1;
            default: log_cb(RETRO_LOG_ERROR, "Wrong data-format, corrupt or unsupported ROM.\n"); return 1;
         }

#if 0
         switch (header[0x0148])
         {
            case 0x00: rombanks = 2; break;
            case 0x01: rombanks = 4; break;
            case 0x02: rombanks = 8; break;
            case 0x03: rombanks = 16; break;
            case 0x04: rombanks = 32; break;
            case 0x05: rombanks = 64; break;
            case 0x06: rombanks = 128; break;
            case 0x07: rombanks = 256; break;
            case 0x08: rombanks = 512; break;
            case 0x52: rombanks = 72; break;
            case 0x53: rombanks = 80; break;
            case 0x54: rombanks = 96; break;
            default: return 1;
         }

         std::printf("rombanks: %u\n", rombanks);*/
#endif

         switch (header[0x0149])
         {
            case 0x00:
               /*std::puts("No RAM");*/
               rambanks = cartridgeType(header[0x0147]) == MBC2;
               break;
            case 0x01:
               /*std::puts("2kB RAM");*/ /*rambankrom=1; break;*/
            case 0x02:
               /*std::puts("8kB RAM");*/
               rambanks = 1;
               break;
            case 0x03:
               /*std::puts("32kB RAM");*/
               rambanks = 4;
               break;
            case 0x04:
               /*std::puts("128kB RAM");*/
               rambanks = 16;
               break;
            case 0x05:
               /*std::puts("undocumented kB RAM");*/
               rambanks = 16;
               break;
            default:
               /*std::puts("Wrong data-format, corrupt or unsupported ROM loaded.");*/
               rambanks = 16;
               break;
         }

         cgb = header[0x0143] >> 7 & (1 ^ forceDmg);
         log_cb(RETRO_LOG_INFO, "cgb: %d\n", cgb);
      }

      log_cb(RETRO_LOG_INFO, "rambanks: %u\n", rambanks);

      rombanks = pow2ceil(rom.size() / 0x4000);
      log_cb(RETRO_LOG_INFO, "rombanks: %u\n", static_cast<unsigned>(rom.size() / 0x4000));

      memptrs_.reset(rombanks, rambanks, cgb ? 8 : 2);

      rom.rewind();
      rom.read(reinterpret_cast<char*>(memptrs_.romdata()), (rom.size() / 0x4000) * 0x4000ul);
      // In case rombanks isn't a power of 2, allocate a disabled area for invalid rombank addresses.
      std::memset(memptrs_.romdata() + (rom.size() / 0x4000) * 0x4000ul, 0xFF, (rombanks - rom.size() / 0x4000) * 0x4000ul);
      enforce8bit(memptrs_.romdata(), rombanks * 0x4000ul);

      if ((multi64rom_ = !rambanks && rombanks == 64 && cartridgeType(memptrs_.romdata()[0x147]) == MBC1 && multiCartCompat))
         log_cb(RETRO_LOG_INFO, "Multi-ROM \"MBC1\" presumed");

      if (rom.fail())
         return 1;

      return 0;
   }

   static bool hasBattery(unsigned char headerByte0x147)
   {
      switch (headerByte0x147)
      {
         case 0x03:
         case 0x06:
         case 0x09:
         case 0x0F:
         case 0x10:
         case 0x13:
         case 0x1B:
         case 0x1E:
         case 0xFF:
            return true;
         default:
            return false;
      }
   }

   static int asHex(const char c)
   {
      return c >= 'A' ? c - 'A' + 0xA : c - '0';
   }

   static bool isAddressWithinAreaRombankCanBeMappedTo(unsigned addr, unsigned bank)
   {
      return (addr< 0x4000) == (bank == 0);
   }

   void *Cartridge::savedata_ptr()
   {
      // Check ROM header for battery.
      if (hasBattery(memptrs_.romdata()[0x147]))
         return memptrs_.rambankdata();
      return 0;
   }

   unsigned Cartridge::savedata_size()
   {
      if (hasBattery(memptrs_.romdata()[0x147]))
         return memptrs_.rambankdataend() - memptrs_.rambankdata();
      return 0;
   }

   void *Cartridge::rtcdata_ptr()
   {
      if (hasRtc(memptrs_.romdata()[0x147]))
         return &rtc_.getBaseTime();
      return 0;
   }

   unsigned Cartridge::rtcdata_size()
   { 
      if (hasRtc(memptrs_.romdata()[0x147]))
         return sizeof(rtc_.getBaseTime());
      return 0;
   }

   void Cartridge::applyGameGenie(const std::string &code)
   {
      if (6 < code.length())
      {
         const unsigned val = (asHex(code[0]) << 4 | asHex(code[1])) & 0xFF;
         const unsigned addr = (asHex(code[2]) << 8 | asHex(code[4]) << 4 | asHex(code[5]) | (asHex(code[6]) ^ 0xF) << 12) & 0x7FFF;
         unsigned cmp = 0xFFFF;

         if (10 < code.length())
         {
            cmp = (asHex(code[8]) << 4 | asHex(code[10])) ^ 0xFF;
            cmp = ((cmp >> 2 | cmp << 6) ^ 0x45) & 0xFF;
         }

         for (unsigned bank = 0; bank < static_cast<std::size_t>(memptrs_.romdataend() - memptrs_.romdata()) / 0x4000; ++bank)
         {
            if (isAddressWithinAreaRombankCanBeMappedTo(addr, bank)
                  && (cmp > 0xFF || memptrs_.romdata()[bank * 0x4000ul + (addr & 0x3FFF)] == cmp))
            {
               ggUndoList_.push_back(AddrData(bank * 0x4000ul + (addr & 0x3FFF), memptrs_.romdata()[bank * 0x4000ul + (addr & 0x3FFF)]));
               memptrs_.romdata()[bank * 0x4000ul + (addr & 0x3FFF)] = val;
            }
         }
      }
   }

   void Cartridge::clearCheats()
   {
      ggUndoList_.clear();
   }

   void Cartridge::setGameGenie(const std::string &codes)
   {
      //if (loaded()) {
      for (std::vector<AddrData>::reverse_iterator it = ggUndoList_.rbegin(), end = ggUndoList_.rend(); it != end; ++it)
      {
         if (memptrs_.romdata() + it->addr < memptrs_.romdataend())
            memptrs_.romdata()[it->addr] = it->data;
      }

      ggUndoList_.clear();

      std::string code;
      for (std::size_t pos = 0; pos < codes.length()
            && (code = codes.substr(pos, codes.find(';', pos) - pos), true); pos += code.length() + 1)
         applyGameGenie(code);
      //}
   }

}

