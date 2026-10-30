// EF08Q: three user-local original TR-DOS 16K firmware resources.
// Resolve relative to the running .app, never CWD or the last browsed ROM.
#ifndef EP128EMU_GUI_BETADISK_BUNDLED_ROM_HPP
#define EP128EMU_GUI_BETADISK_BUNDLED_ROM_HPP
#ifdef __APPLE__
#include "betadisk_trial_media.hpp"
#include <mach-o/dyld.h>
#include <CommonCrypto/CommonDigest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>
namespace Ep128EmuBetaTrial {
inline const char *bundledFileName(int variant) {
  switch(variant) {
  case 503: return "TRDOS-5.03.rom";
  case 504: return "TRDOS-5.04.rom";
  case 604: return "TRDOS-6.04.rom";
  default: throw std::runtime_error("Beta Disk: unknown bundled ROM variant");
  }
}
inline const char *bundledSHA256(int variant) {
  switch(variant) {
  case 503: return "91259fca6a8ded428cc24046f5b48b31d4043f2afbd9087d8946eaf4e10d71a5";
  case 504: return "e21d37271d087eab5ef8f88d8f3a58c8c19da1fa857b9790eaa974b231db9e13";
  case 604: return "1a029b414f474dfa80dfa4272338e1dd23b7d08615be9db127a2404e2b5e5acf";
  default: throw std::runtime_error("Beta Disk: unknown bundled ROM variant");
  }
}
inline std::string bundledROMPathFromExecutable(const std::string& exe,int variant) {
  const std::string marker("/Contents/MacOS/");
  size_t n=exe.rfind(marker);
  if(n==std::string::npos)
    throw std::runtime_error("Beta Disk: bundled firmware requires a macOS .app");
  return exe.substr(0,n)+"/Contents/Resources/BetaDisk/"+bundledFileName(variant);
}
inline std::string bundledROMPath(int variant) {
  uint32_t n=0;
  (void)_NSGetExecutablePath((char*)0,&n);
  if(n<2U || n>65536U)
    throw std::runtime_error("Beta Disk: cannot locate running application");
  std::vector<char> buf(size_t(n)+1U,'\0');
  if(_NSGetExecutablePath(buf.data(),&n)!=0)
    throw std::runtime_error("Beta Disk: cannot locate running executable");
  return bundledROMPathFromExecutable(buf.data(),variant);
}
inline std::string digestROM(const std::vector<uint8_t>& rom) {
  if (rom.size()!=16384U) throw std::runtime_error("Beta Disk: ROM size invalid");
  unsigned char hash[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(rom.data(),CC_LONG(rom.size()),hash);
  char hex[2*CC_SHA256_DIGEST_LENGTH+1];
  for(size_t i=0;i<CC_SHA256_DIGEST_LENGTH;++i)
    std::snprintf(hex+2*i,sizeof(hex)-2*i,"%02x",unsigned(hash[i]));
  return std::string(hex,2*CC_SHA256_DIGEST_LENGTH);
}
inline std::vector<uint8_t> readBundledROMFromExecutable(const std::string& exe,int variant) {
  // Derive the path first; verify exact original content before VM install.
  std::vector<uint8_t> rom=readROM(bundledROMPathFromExecutable(exe,variant));
  if(digestROM(rom)!=bundledSHA256(variant))
    throw std::runtime_error("Beta Disk: bundled TR-DOS ROM checksum mismatch");
  return rom;
}
inline std::vector<uint8_t> readBundledROM(int variant) {
  std::vector<uint8_t> rom=readROM(bundledROMPath(variant));
  if(digestROM(rom)!=bundledSHA256(variant))
    throw std::runtime_error("Beta Disk: bundled TR-DOS ROM checksum mismatch");
  return rom;
}
} // namespace Ep128EmuBetaTrial
#elif defined(WIN32)
#include "betadisk_trial_media.hpp"
#include "speccydos_trial_media.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
namespace Ep128EmuBetaTrial {
inline const char *bundledFileName(int v) {
  switch(v){case 503:return "TRDOS-5.03.rom";case 504:return "TRDOS-5.04.rom";
            case 604:return "TRDOS-6.04.rom";default:throw std::runtime_error("Beta Disk: invalid TR-DOS preset");}
}
inline const char *bundledSHA256(int v) {
  switch(v){case 503:return "91259fca6a8ded428cc24046f5b48b31d4043f2afbd9087d8946eaf4e10d71a5";
            case 504:return "e21d37271d087eab5ef8f88d8f3a58c8c19da1fa857b9790eaa974b231db9e13";
            case 604:return "1a029b414f474dfa80dfa4272338e1dd23b7d08615be9db127a2404e2b5e5acf";
            default:throw std::runtime_error("Beta Disk: invalid preset");}
}
inline std::string bundledROMPathFromExecutable(const std::string& exe,int variant) {
  size_t sep=exe.find_last_of("\\/");
  if(sep==std::string::npos)throw std::runtime_error("Beta Disk: executable folder unavailable");
  return exe.substr(0,sep)+"\\BetaDisk\\"+bundledFileName(variant);
}
inline std::string bundledROMPath(int variant) {
  wchar_t executable[32768]={0};
  DWORD count=GetModuleFileNameW(0,executable,DWORD(sizeof(executable)/sizeof(executable[0])));
  if(!count || count>=sizeof(executable)/sizeof(executable[0]))
    throw std::runtime_error("Beta Disk: cannot locate executable");
  return bundledROMPathFromExecutable(Ep128EmuSpeccyTrial::utf8Path(executable),variant);
}
inline std::string digestROM(const std::vector<uint8_t>& rom) {
  if(rom.size()!=16384)throw std::runtime_error("Beta Disk: firmware must be 16K");
  HCRYPTPROV prov=0;HCRYPTHASH h=0;
  if(!CryptAcquireContextW(&prov,0,0,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))
    throw std::runtime_error("Beta Disk: Win7 CryptoAPI SHA256 initialization failed");
  if(!CryptCreateHash(prov,CALG_SHA_256,0,0,&h)) {
    CryptReleaseContext(prov,0);throw std::runtime_error("Beta Disk: cannot create SHA256 digest");
  }
  BYTE bytes[32]={0};DWORD length=sizeof(bytes);
  const bool good=CryptHashData(h,rom.data(),DWORD(rom.size()),0)!=FALSE &&
      CryptGetHashParam(h,HP_HASHVAL,bytes,&length,0)!=FALSE && length==sizeof(bytes);
  CryptDestroyHash(h);CryptReleaseContext(prov,0);
  if(!good)throw std::runtime_error("Beta Disk: SHA256 failed");
  char text[65]={0};for(size_t i=0;i<sizeof(bytes);++i)std::snprintf(text+i*2,3,"%02x",unsigned(bytes[i]));
  return std::string(text,64);
}
inline std::vector<uint8_t> readBundledROMFromExecutable(const std::string& exe,int v) {
  std::vector<uint8_t> rom=readROM(bundledROMPathFromExecutable(exe,v));
  if(digestROM(rom)!=bundledSHA256(v))throw std::runtime_error("Beta Disk: bundled TR-DOS SHA256 mismatch");
  return rom;
}
inline std::vector<uint8_t> readBundledROM(int v) {
  std::vector<uint8_t> rom=readROM(bundledROMPath(v));
  if(digestROM(rom)!=bundledSHA256(v))throw std::runtime_error("Beta Disk: bundled TR-DOS SHA256 mismatch");
  return rom;
}
} // namespace Ep128EmuBetaTrial
#endif // __APPLE__ || WIN32
#if defined(__APPLE__) || defined(WIN32)
namespace Ep128EmuBetaTrial {
// Only guest-verified firmware can mount a persistent writable TRD clone.
// Arbitrary external ROMs and the known-bad 5.04 dump remain read-only.
inline bool writableROMApproved(const std::vector<uint8_t>& rom) {
  const std::string sha=digestROM(rom);
  return sha==bundledSHA256(503) || sha==bundledSHA256(504) ||
         sha==bundledSHA256(604);
}
} // namespace Ep128EmuBetaTrial
#endif
#endif
