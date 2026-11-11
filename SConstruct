# vim: syntax=python

from __future__ import print_function
import sys, os, platform, subprocess

win64CrossCompile = int(ARGUMENTS.get('win64', 0))
mingwCrossCompile = win64CrossCompile or int(ARGUMENTS.get('win32', 0))
hostMacBuild = (sys.platform[:6] == 'darwin' and not mingwCrossCompile)
linux32CrossCompile = int(ARGUMENTS.get('linux32', 0))
# on Linux, statically linked SDL version >= 1.2.10 that was not built
# without video support (--disable-video) breaks FLTK
disableSDL = int(ARGUMENTS.get('nosdl', 0))
disableLua = int(ARGUMENTS.get('nolua', 0))
buildUtilities = int(ARGUMENTS.get('utils', 1))
enableGLShaders = int(ARGUMENTS.get('glshaders', 1))
enableDebug = int(ARGUMENTS.get('debug', 0))
buildRelease = not enableDebug and int(ARGUMENTS.get('release', 1))
# for mingwCrossCompile, use LuaJIT instead of Lua 5.3
useLuaJIT = int(ARGUMENTS.get('luajit', 0))
cmosZ80 = int(ARGUMENTS.get('z80cmos', 0))
# build with experimental SD card emulation
enableSDExt = int(ARGUMENTS.get('sdext', 1))
# enable SID card emulation
enableReSID = int(ARGUMENTS.get('resid', 1))
# enable MIDI port emulation
enableMIDI = int(ARGUMENTS.get('midi', 0))
# use cURL library in makecfg to download the ROM package
enableCURL = int(ARGUMENTS.get('curl', int(not mingwCrossCompile)))
userFlags = ARGUMENTS.get('cflags', '')
# Targeted adaptation of czo/ep128emu 096c820 (arch) and e3bcf10 (minmacos).
userArch = ARGUMENTS.get('arch', '').strip()
userMinMacosVersion = ARGUMENTS.get('minmacos', '').strip()
# Only the separate 2.2 trial worktree may request the pinned Universal deps.
universalDepsMode = hostMacBuild and int(ARGUMENTS.get('universaldeps', 0))
macUniversalDepsRoot = (os.path.realpath(os.path.abspath('build/universal-deps'))
                        if universalDepsMode else None)
if int(ARGUMENTS.get('universaldepscheck', 0)) and not universalDepsMode:
    Exit('UNIVERSAL_DEPS_CHECK_REQUIRES_UNIVERSAL_DEPS_OPT_IN')
disablePkgConfig = int(ARGUMENTS.get('nopkgconfig',
                                     int(linux32CrossCompile or \
                                         mingwCrossCompile)))
enableBuildCache = int(ARGUMENTS.get('cache', 0))

compilerFlags = ''
if buildRelease:
    if linux32CrossCompile or (mingwCrossCompile and not win64CrossCompile):
        compilerFlags = ' -march=pentium2 '
if enableDebug and not buildRelease:
    compilerFlags = ' -Wno-long-long -Wshadow -g -O0 ' + compilerFlags
    compilerFlags = ' -Wall -W -pedantic ' + compilerFlags
else:
    compilerFlags = ' -Wall -O3 ' + compilerFlags
    if (platform.machine().lower().startswith('armv7')):
        compilerFlags = compilerFlags + ' -mtune=generic-armv7-a '
    else:
        compilerFlags = compilerFlags + ' -mtune=generic '
    compilerFlags = compilerFlags + ' -fno-inline-functions '
    compilerFlags = compilerFlags + ' -fomit-frame-pointer -ffast-math '

# -----------------------------------------------------------------------------

# pkgname : [ pkgconfig, [ package_names ],
#             linux_flags, mingw_flags, c_header, cxx_header, optional ]

fltkLibsLinux = '-lfltk -lfltk_images -lfltk_jpeg -lfltk_png'
fltkLibsMinGW = fltkLibsLinux.replace(' -lfltk_jpeg', '').replace(' -lfltk_png', '') + ' -lz -lcomdlg32 -lcomctl32 -lole32'
fltkLibsMinGW = fltkLibsMinGW + ' -luuid -lws2_32 -lwinmm -lgdi32 -ladvapi32 -lshell32'
if mingwCrossCompile and not win64CrossCompile:
    # Win7/MinGW32 FLTK 1.4.5 pulls these native 32-bit import libraries.
    # Preserve the accepted UCRT64 Windows x64 and macOS linker paths.
    fltkLibsMinGW += ' -lgdiplus -lwinspool'
fltkLibsLinux = fltkLibsLinux + ' -lfltk_z -lXcursor -lXinerama -lXrender'
fltkLibsLinux = fltkLibsLinux + ' -lXext -lXft -lXfixes -lX11 -lfontconfig -ldl'

packageConfigs = {
    'FLTK' : [
        'fltk-config --use-images --cflags --cxxflags --ldflags', [''],
        fltkLibsLinux, fltkLibsMinGW, '', 'FL/Fl.H', 0 ],
    'FLTK-GL' : [
        'fltk-config --use-gl --use-images --cflags --cxxflags --ldflags', [''],
        '-lfltk_gl ' + fltkLibsLinux + ' -lGL',
        '-lfltk_gl ' + fltkLibsMinGW + ' -lopengl32'
        + (' -lglu32' if mingwCrossCompile and not win64CrossCompile else ''),
        '', 'FL/Fl_Gl_Window.H', 1 ],
    'sndfile' : [
        'pkg-config --silence-errors --cflags --libs',
        ['sndfile'],
        '-lsndfile', '-lsndfile', 'sndfile.h', '', 0 ],
    'PortAudio' : [
        'pkg-config --silence-errors --cflags --libs',
        ['portaudio-2.0', 'portaudio2', 'portaudio'],
        '-lportaudio -lasound -lm -ldl -lpthread -lrt', '-lportaudio -lwinmm',
        'portaudio.h', '', 0 ],
    'Lua' : [
        'pkg-config --silence-errors --cflags --libs',
        ['lua-5.3', 'lua5.3', 'lua53', 'lua-5.2', 'lua5.2', 'lua52', 'lua-5.1', 'lua5.1', 'lua51', 'lua'],
        '-llua', '', 'lua.h', '', 1 ],
    'SDL' : [
        'pkg-config --silence-errors --cflags --libs',
        ['sdl', 'sdl1'],
        '-lSDL', '-lSDL -lwinmm', 'SDL/SDL.h', '', 1],
    'cURL' : [
        'pkg-config --silence-errors --cflags --libs',
        ['libcurl', 'curl'],
        '-lcurl -lssl -lcrypto', '-lcurldll',
        'curl/curl.h', '', 1]
}


# The default/macOS ARM64 and all Windows builds retain their original
# package resolution. 2.2 Universal is a separate explicit opt-in.
if universalDepsMode:
    luaPrefix = os.path.join(macUniversalDepsRoot, 'lua-fat')
    packageConfigs['Lua'][0] = ('echo -I' + luaPrefix + '/include -L'
                                 + luaPrefix + '/lib -llua')
    packageConfigs['Lua'][1] = ['']
    # Homebrew has an actual sdl.pc and overrides the Provides: sdl
    # metadata in the pinned sdl12_compat.pc. Request exact package.
    packageConfigs['SDL'][1] = ['sdl12_compat']

# Mac-only toolchain contract. Never silently fall back to /usr/local FLTK 1.3
# or Homebrew when building in a new Git worktree or resuming SCons directly.
# All failures must occur before SCons compiles or links any source files.
macPinnedFLTKPrefix = None
macPinnedFLTKConfig = None
macPinnedFLTKFluid = None
if hostMacBuild:
    macPinnedFLTKPrefix = os.path.realpath(os.path.expanduser(
        '~/Developer/EP128/EP128EMU/third_party/fltk-1.4.5'))
    macNativeFLTKPrefix = macPinnedFLTKPrefix
    # Explicit 2.2 trial opt-in only. Keep the accepted ARM64 prefix untouched.
    # The alternate FLTK is built from the *same pinned 1.4.5 source tarball*.
    # The native ARM64 fluid remains the build-time GUI compiler.
    if int(ARGUMENTS.get('universaldeps', 0)):
        if sorted(userArch.split()) != ['arm64', 'x86_64']:
            Exit('UNIVERSAL_DEPS_REQUIRES_BOTH_ARCHES')
        macPinnedFLTKPrefix = os.path.realpath(os.path.abspath(
            'build/universal-deps/fltk-1.4.5-fat'))
    macPinnedFLTKConfig = os.path.join(macPinnedFLTKPrefix, 'bin', 'fltk-config')
    macPinnedFLTKFluid = os.path.join(macNativeFLTKPrefix, 'bin', 'fluid')
    for var, required in (('EP128EMU_FLTK_PREFIX', macPinnedFLTKPrefix),
                          ('EP128EMU_FLTK_CONFIG', macPinnedFLTKConfig),
                          ('EP128EMU_FLUID', macPinnedFLTKFluid)):
        supplied = os.environ.get(var)
        if supplied and os.path.realpath(os.path.expanduser(supplied)) != required:
            Exit('FLTK_PIN_MISMATCH: %s=%s; required %s' %
                 (var, supplied, required))
    for fileName in (macPinnedFLTKConfig, macPinnedFLTKFluid,
                     os.path.join(macPinnedFLTKPrefix, 'include', 'FL', 'Fl.H')):
        if not os.path.isfile(fileName):
            Exit('FLTK_PIN_MISSING: %s' % fileName)
    try:
        fltkVersion = subprocess.check_output(
            [macPinnedFLTKConfig, '--version'],
            stderr=subprocess.STDOUT).decode('utf-8').strip()
        fltkInstalledPrefix = subprocess.check_output(
            [macPinnedFLTKConfig, '--prefix'],
            stderr=subprocess.STDOUT).decode('utf-8').strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        Exit('FLTK_PIN_EXEC_FAILURE: %s' % exc)
    if fltkVersion != '1.4.5' or os.path.realpath(fltkInstalledPrefix) != macPinnedFLTKPrefix:
        Exit('FLTK_PIN_VERSION_OR_PREFIX_MISMATCH: version=%s prefix=%s' %
             (fltkVersion, fltkInstalledPrefix))
    macFLTKWrapper = os.path.abspath('tools/fltk-config')
    if not os.path.isfile(macFLTKWrapper) or not os.access(macFLTKWrapper, os.X_OK):
        Exit('FLTK_PIN_WRAPPER_MISSING: %s' % macFLTKWrapper)
    packageConfigs['FLTK'][0] = (
        macFLTKWrapper + ' --use-images --cflags --cxxflags --ldflags')
    packageConfigs['FLTK-GL'][0] = (
        macFLTKWrapper + ' --use-gl --use-images --cflags --cxxflags --ldflags')
    # Standalone preflight: `scons -Q fltkcheck=1` checks the actual wrapper
    # flags and fluid executable without triggering Configure() or compilation.
    # Run these checks on normal builds as well, so a broken wrapper fails early.
    fltkTestEnv = dict(os.environ)
    fltkTestEnv['EP128EMU_FLTK_PREFIX'] = macPinnedFLTKPrefix
    fltkTestEnv['EP128EMU_FLTK_CONFIG'] = macPinnedFLTKConfig
    fltkTestEnv['PATH'] = os.pathsep.join((os.path.abspath('tools'),
                                           os.path.join(macPinnedFLTKPrefix, 'bin'),
                                           os.environ.get('PATH', '')))
    try:
        fltkFlags = subprocess.check_output(
            [macFLTKWrapper, '--use-images', '--cflags', '--cxxflags', '--ldflags'],
            env=fltkTestEnv, stderr=subprocess.STDOUT).decode('utf-8')
        fluidVersion = subprocess.check_output(
            [macPinnedFLTKFluid, '--version'],
            env=fltkTestEnv, stderr=subprocess.STDOUT).decode('utf-8').strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        Exit('FLTK_PIN_TOOL_FAILURE: %s' % exc)
    if ('-I' + os.path.join(macPinnedFLTKPrefix, 'include')) not in fltkFlags or \
       ('-L' + os.path.join(macPinnedFLTKPrefix, 'lib')) not in fltkFlags or \
       fluidVersion != 'fluid v1.4.5':
        Exit('FLTK_PIN_TOOL_MISMATCH: invalid wrapper include/lib or fluid version')
    print('FLTK_PIN_1_4_5_PREFLIGHT_PASS: ' + macPinnedFLTKPrefix)
    if universalDepsMode:
        # Fail before source compilation if a dependency or architecture
        # slice is absent. No silent Homebrew ARM64 fallback.
        fatLibs = (
            ('portaudio', 'libportaudio.dylib'),
            ('libsndfile', 'libsndfile.dylib'),
            ('flac', 'libFLAC.dylib'),
            ('libogg', 'libogg.dylib'),
            ('libvorbis', 'libvorbis.dylib'),
            ('lame', 'libmp3lame.dylib'),
            ('lua', 'liblua.dylib'),
            ('sdl3', 'libSDL3.dylib'),
            ('sdl2compat', 'libSDL2.dylib'),
            ('sdl12compat', 'libSDL.dylib'),
            ('sdl12compat', 'libSDLmain.a'))
        for prefixName, libName in fatLibs:
            filename = os.path.join(macUniversalDepsRoot,
                                    prefixName + '-fat', 'lib', libName)
            if not os.path.isfile(filename):
                Exit('UNIVERSAL_DEP_MISSING: ' + filename)
            try:
                slices = subprocess.check_output(
                    ['lipo', '-archs', filename],
                    stderr=subprocess.STDOUT).decode('utf-8').split()
            except (OSError, subprocess.CalledProcessError) as exc:
                Exit('UNIVERSAL_DEP_LIPO_ERROR: %s' % exc)
            if sorted(slices) != ['arm64', 'x86_64']:
                Exit('UNIVERSAL_DEP_SLICES_MISSING: %s: %s' %
                     (filename, slices))
        pkgPaths = [os.path.join(macUniversalDepsRoot, n + '-fat',
                                 'lib', 'pkgconfig')
                    for n in ('sdl12compat', 'sdl2compat', 'sdl3',
                              'libsndfile', 'portaudio', 'flac',
                              'libogg', 'libvorbis', 'opus', 'mpg123')]
        trialPkgEnv = dict(os.environ)
        trialPkgEnv['PKG_CONFIG_PATH'] = os.pathsep.join(pkgPaths)
        for pkg, prefix in (('sdl12_compat', 'sdl12compat'), ('sndfile', 'libsndfile'),
                            ('portaudio-2.0', 'portaudio')):
            try:
                actual = subprocess.check_output(
                    ['pkg-config', '--variable=prefix', pkg],
                    env=trialPkgEnv,
                    stderr=subprocess.STDOUT).decode('utf-8').strip()
            except (OSError, subprocess.CalledProcessError) as exc:
                Exit('UNIVERSAL_DEPS_PKGCONFIG_MISSING: %s: %s' % (pkg, exc))
            expected = os.path.join(macUniversalDepsRoot, prefix + '-fat')
            if os.path.realpath(actual) != os.path.realpath(expected):
                Exit('UNIVERSAL_DEPS_PKGCONFIG_WRONG_ORIGIN: %s=%s expected %s'
                     % (pkg, actual, expected))
        print('UNIVERSAL_DEPS_SOURCE_PIN_AND_ARCH_GATE_PASS: 11 artifacts')
        if int(ARGUMENTS.get('universaldepscheck', 0)):
            Exit(0)
    if int(ARGUMENTS.get('fltkcheck', 0)):
        print('FLTK_PIN_STANDALONE_CHECK_PASS')
        Exit(0)

def configurePackage(env, pkgName):
    global packageConfigs, disablePkgConfig
    global linux32CrossCompile, mingwCrossCompile, win64CrossCompile
    if not disablePkgConfig:
        for s in packageConfigs[pkgName][1]:
            if not s:
                print('Checking for package ' + pkgName + '...', end = ' ')
                # hack to work around fltk-config adding unwanted compiler flags
                savedCFlags = env['CCFLAGS']
                savedCXXFlags = env['CXXFLAGS']
            else:
                print('Checking for package ' + s + '...', end = ' ')
                s = ' ' + s
            try:
                # SCons 4.x ParseConfig() mutates the environment in place and
                # returns None on success; older ep128emu expected a truthy value.
                env.ParseConfig(packageConfigs[pkgName][0] + s)
                print('yes')
                if not s:
                    env['CCFLAGS'] = savedCFlags
                    env['CXXFLAGS'] = savedCXXFlags
                    if ['_FORTIFY_SOURCE', '2'] in env['CPPDEFINES']:
                        env['CPPDEFINES'].remove(['_FORTIFY_SOURCE', '2'])
                return 1
            except:
                print('no')
                continue
        pkgFound = 0
    else:
        configure = env.Configure()
        if packageConfigs[pkgName][4]:
            pkgFound = configure.CheckCHeader(packageConfigs[pkgName][4])
        else:
            pkgFound = configure.CheckCXXHeader(packageConfigs[pkgName][5])
        configure.Finish()
        if pkgFound:
            env.MergeFlags(
                packageConfigs[pkgName][2 + int(bool(mingwCrossCompile))])
    if not pkgFound:
        if not packageConfigs[pkgName][6]:
            print(' *** error configuring ' + pkgName)
            Exit(-1)
        print('WARNING: package ' + pkgName + ' not found')
        return 0
    return 1

if enableBuildCache:
    CacheDir("./.build_cache")

programNamePrefix = ""
buildingLinuxPackage = 0
if not mingwCrossCompile:
    if sys.platform[:5] == 'linux':
        try:
            instPrefix = os.environ["UB_INSTALLDIR"]
            if instPrefix:
                instPrefix += "/usr"
                buildingLinuxPackage = 1
        except:
            pass
    if not buildingLinuxPackage:
        instPrefix = os.environ["HOME"]
        instShareDir = instPrefix + "/.local/share"
    else:
        instShareDir = instPrefix + "/share"
    instBinDir = instPrefix + "/bin"
    instDataDir = instShareDir + "/ep128emu"
    instPixmapDir = instShareDir + "/pixmaps"
    instDesktopDir = instShareDir + "/applications"
    instROMDir = instDataDir + "/roms"
    instConfDir = instDataDir + "/config"
    instDiskDir = instDataDir + "/disk"
    programNamePrefix = "ep"

oldSConsVersion = 0
try:
    EnsureSConsVersion(0, 97)
except:
    print('WARNING: using old SCons version')
    oldSConsVersion = 1

def copyEnvironment(env):
    if oldSConsVersion:
        return env.Copy()
    return env.Clone()

sconsEnv = { 'PATH' : os.environ['PATH'],
             'HOME' : os.environ['HOME'] }
if hostMacBuild:
    # Force the wrapper and pinned fluid ahead of system-wide binaries,
    # including for SCons subprocesses, even when the caller's PATH is stale.
    sconsEnv['PATH'] = os.pathsep.join((os.path.abspath('tools'),
                                      os.path.join(macPinnedFLTKPrefix, 'bin'),
                                      os.environ['PATH']))
    sconsEnv['EP128EMU_FLTK_PREFIX'] = macPinnedFLTKPrefix
    sconsEnv['EP128EMU_FLTK_CONFIG'] = macPinnedFLTKConfig
    sconsEnv['EP128EMU_FLUID'] = macPinnedFLTKFluid
    if universalDepsMode:
        # pkg-config sees our original pinned 2.2 libraries first, not brew.
        sconsEnv['PKG_CONFIG_PATH'] = os.pathsep.join(pkgPaths)
else:
    for k in ('EP128EMU_FLTK_PREFIX', 'EP128EMU_FLTK_CONFIG'):
        if k in os.environ:
            sconsEnv[k] = os.environ[k]
DefaultEnvironment(ENV = sconsEnv)
ep128emuLibEnvironment = Environment(ENV = sconsEnv)
if linux32CrossCompile:
    compilerFlags = ' -m32 ' + compilerFlags
ep128emuLibEnvironment.Append(CCFLAGS = Split(compilerFlags))
ep128emuLibEnvironment.Append(CPPPATH = ['.', './src'])
if userFlags:
    ep128emuLibEnvironment.MergeFlags(userFlags)
if not mingwCrossCompile:
    ep128emuLibEnvironment.Append(CPPPATH = ['/usr/local/include'])
if hostMacBuild:
    ep128emuLibEnvironment.Append(CPPPATH = ['/usr/X11R6/include'])
    # Allow a pinned local FLTK prefix for reproducible trial/release builds.
    # Keep Homebrew includes available for the other macOS dependencies.
    fltkPrefix = macPinnedFLTKPrefix
    ep128emuLibEnvironment.Prepend(CPPPATH = [fltkPrefix + '/include',
                                              '/opt/homebrew/include'])
    # FLTK 1.4 dylibs use @rpath for their internal dependencies.
    ep128emuLibEnvironment.Append(
        LINKFLAGS = ['-Wl,-rpath,' + fltkPrefix + '/lib'])
    if universalDepsMode:
        # Complete codec/SDL dylib closure for the *trial* build only.
        # Final .app packaging must replace absolute paths with bundle paths.
        libPrefixes = ('sdl12compat', 'sdl2compat', 'sdl3', 'lua',
                       'portaudio', 'libsndfile', 'lame', 'flac',
                       'libogg', 'libvorbis', 'opus', 'mpg123')
        prefixPaths = [os.path.join(macUniversalDepsRoot, x + '-fat')
                       for x in libPrefixes]
        ep128emuLibEnvironment.Prepend(
            CPPPATH = [x + '/include' for x in prefixPaths])
        ep128emuLibEnvironment.Prepend(
            LIBPATH = [x + '/lib' for x in prefixPaths])
        ep128emuLibEnvironment.Append(
            LINKFLAGS = ['-Wl,-rpath,' + x + '/lib' for x in prefixPaths])
if not linux32CrossCompile:
    linkFlags = ' -L. '
else:
    linkFlags = ' -m32 -L. -L/usr/X11R6/lib '
ep128emuLibEnvironment.Append(LINKFLAGS = Split(linkFlags))
if mingwCrossCompile:
    wordSize = ['32', '64'][int(bool(win64CrossCompile))]
    mingwPrefix = os.environ.get('EP128EMU_MINGW_PREFIX',
                                 (sys.prefix if sys.platform[:3] == 'win' and os.environ.get('MSYSTEM')
                                  else 'C:/mingw' + wordSize))
    ep128emuLibEnvironment.Prepend(CCFLAGS = ['-m' + wordSize])
    ep128emuLibEnvironment.Prepend(LINKFLAGS = ['-m' + wordSize])
    ep128emuLibEnvironment.Append(CPPPATH = [mingwPrefix + '/include'])
    if sys.platform[:3] == 'win':
        toolNamePrefix = ''
    elif win64CrossCompile:
        toolNamePrefix = 'x86_64-w64-mingw32-'
    else:
        toolNamePrefix = 'i686-w64-mingw32-'
    ep128emuLibEnvironment['AR'] = toolNamePrefix + 'ar'
    ep128emuLibEnvironment['CC'] = toolNamePrefix + 'gcc'
    ep128emuLibEnvironment['CPP'] = toolNamePrefix + 'cpp'
    ep128emuLibEnvironment['CXX'] = toolNamePrefix + 'g++'
    ep128emuLibEnvironment['LINK'] = toolNamePrefix + 'g++'
    ep128emuLibEnvironment['RANLIB'] = toolNamePrefix + 'ranlib'
    ep128emuLibEnvironment['PROGSUFFIX'] = '.exe'
    packageConfigs['Lua'][3] = '-llua5.3' if not useLuaJIT else '-llua51'
    ep128emuLibEnvironment.Append(
        CPPPATH = [mingwPrefix
                   + '/include/lua' + ['5.3', '5.1'][int(bool(useLuaJIT))]])
    ep128emuLibEnvironment.Append(LIBS = ['user32', 'kernel32'])
    ep128emuLibEnvironment.Prepend(CCFLAGS = ['-mthreads'])
    ep128emuLibEnvironment.Prepend(LINKFLAGS = ['-mthreads'])
elif sys.platform[:5] == 'linux':
    if enableMIDI:
        ep128emuLibEnvironment.Prepend(CCFLAGS = ['-DPMALSA=1'])
    ep128emuLibEnvironment.Prepend(LINKFLAGS = ['-Wl,--as-needed'])
if buildRelease:
    ep128emuLibEnvironment.Append(LINKFLAGS = ['-s'])
if enableMIDI:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DENABLE_MIDI_PORT'])
    ep128emuLibEnvironment.Append(CPPPATH = ['./portmidi/pm_common',
                                             './portmidi/porttime'])

# Czo's multi-architecture SCons flags must remain paired (-arch, value).
# Do not turn on multiarch implicitly: dependency slices are a separate gate.
if userArch:
    arches = userArch.split()
    if not hostMacBuild or len(set(arches)) != len(arches) or any(
            arch not in ('arm64', 'x86_64') for arch in arches):
        Exit('MAC_ARCH_INVALID: expected distinct arm64 and/or x86_64 on macOS')
    for arch in arches:
        ep128emuLibEnvironment.Append(CCFLAGS = [['-arch', arch]])
        ep128emuLibEnvironment.Append(LINKFLAGS = [['-arch', arch]])
if userMinMacosVersion:
    import re
    if not hostMacBuild or not re.fullmatch(r'[0-9]{1,2}\.[0-9]{1,2}(?:\.[0-9]{1,2})?', userMinMacosVersion):
        Exit('MAC_MIN_VERSION_INVALID: expected e.g. minmacos=11.0 on macOS')
    macMinFlag = '-mmacosx-version-min=' + userMinMacosVersion
    ep128emuLibEnvironment.Append(CCFLAGS = [macMinFlag])
    ep128emuLibEnvironment.Append(LINKFLAGS = [macMinFlag])

ep128emuGUIEnvironment = copyEnvironment(ep128emuLibEnvironment)
if mingwCrossCompile:
    ep128emuGUIEnvironment.Prepend(LINKFLAGS = ['-mwindows'])
else:
    ep128emuGUIEnvironment.Append(LIBS = ['pthread'])
configurePackage(ep128emuGUIEnvironment, 'FLTK')
makecfgEnvironment = copyEnvironment(ep128emuGUIEnvironment)
configurePackage(ep128emuGUIEnvironment, 'sndfile')
tapeeditEnvironment = copyEnvironment(ep128emuGUIEnvironment)
if hostMacBuild:
    ep128emuGUIEnvironment.Append(LIBS = ['mp3lame'])
    ep128emuGUIEnvironment.Append(LINKFLAGS = ['-framework', 'IOKit'])
haveLua = 0
haveSDL = 0
if not disableLua:
    haveLua = configurePackage(ep128emuGUIEnvironment, 'Lua')
if not disableSDL:
    haveSDL = configurePackage(ep128emuGUIEnvironment, 'SDL')
configurePackage(ep128emuGUIEnvironment, 'PortAudio')

ep128emuGLGUIEnvironment = copyEnvironment(ep128emuGUIEnvironment)
disableOpenGL = 1
if configurePackage(ep128emuGLGUIEnvironment, 'FLTK-GL'):
    configure = ep128emuGLGUIEnvironment.Configure()
    if hostMacBuild:
        haveOpenGL = configure.CheckCHeader('OpenGL/gl.h')
    else:
        haveOpenGL = configure.CheckCHeader('GL/gl.h')
    if haveOpenGL:
        disableOpenGL = 0
        if enableGLShaders:
            if hostMacBuild:
                shaderTest = '''
                    #include <OpenGL/gl.h>
                    int main()
                    {
                      GLuint s = glCreateShader(GL_FRAGMENT_SHADER);
                      glCompileShader(s);
                      return 0;
                    }
                '''
                if not configure.TryLink(shaderTest, '.cpp'):
                    print('WARNING: disabling GL shader support')
                    enableGLShaders = 0
            elif not configure.CheckType('PFNGLCOMPILESHADERPROC',
                                         '#include <GL/gl.h>\n'
                                         + '#include <GL/glext.h>'):
                print('WARNING: disabling GL shader support')
                enableGLShaders = 0
    configure.Finish()
if sys.platform[:5] == 'linux' and not mingwCrossCompile:
    ep128emuGUIEnvironment.Append(LIBS = ['X11'])
    ep128emuGLGUIEnvironment.Append(LIBS = ['GL', 'X11'])
if disableOpenGL:
    print('WARNING: OpenGL is not found, only software video will be supported')
    enableGLShaders = 0
    ep128emuGLGUIEnvironment = copyEnvironment(ep128emuGUIEnvironment)
    ep128emuGLGUIEnvironment.Append(CCFLAGS = ['-DDISABLE_OPENGL_DISPLAY'])

# SCons 4.x ParseConfig mistakes FLTK 1.4.5's `-weak_framework
# UniformTypeIdentifiers` as a source/lib named UniformTypeIdentifiers.
# Keep its intended weak link flag, not a missing file dependency.
if sys.platform[:6] == 'darwin':
    for fwEnv in (ep128emuGUIEnvironment, ep128emuGLGUIEnvironment,
                  makecfgEnvironment, tapeeditEnvironment):
        falseLibs = [x for x in fwEnv['LIBS']
                     if str(x) == 'UniformTypeIdentifiers']
        if falseLibs:
            fwEnv['LIBS'] = [x for x in fwEnv['LIBS']
                             if str(x) != 'UniformTypeIdentifiers']
            fwEnv.Append(LINKFLAGS = [
                '-Wl,-weak_framework,UniformTypeIdentifiers'])

ep128emuLibEnvironment['CPPPATH'] = ep128emuGLGUIEnvironment['CPPPATH']

imageLibTestProgram = '''
    #include <FL/Fl.H>
    #include <FL/Fl_Shared_Image.H>
    int main()
    {
      Fl_Shared_Image *tmp = Fl_Shared_Image::get("foo");
      tmp->release();
      return 0;
    }
'''

def imageLibTest(env1, env2, env3, env4):
    # remove unneeded and possibly conflicting libraries added by fltk-config
    for libName in ['jpeg', 'png']:
        if libName in env1['LIBS']:
            tmpEnv = copyEnvironment(env1)
            tmpEnv['LIBS'].remove(libName)
            tmpConfig = tmpEnv.Configure()
            if tmpConfig.TryLink(imageLibTestProgram, '.cpp'):
                env1['LIBS'].remove(libName)
                env2['LIBS'].remove(libName)
                env3['LIBS'].remove(libName)
                env4['LIBS'].remove(libName)
            tmpConfig.Finish()

if not oldSConsVersion:
    imageLibTest(makecfgEnvironment, tapeeditEnvironment,
                 ep128emuGUIEnvironment, ep128emuGLGUIEnvironment)

configure = ep128emuLibEnvironment.Configure()
if configure.CheckType('PaStreamCallbackTimeInfo', '#include <portaudio.h>'):
    havePortAudioV19 = 1
else:
    havePortAudioV19 = 0
    print('WARNING: using old v18 PortAudio interface')
fltkVersion13 = 0
if configure.CheckCXXHeader('FL/Fl_Cairo.H'):
    fltkVersion13 = 1
else:
    ep128emuLibEnvironment.Append(CPPPATH = ['./Fl_Native_File_Chooser'])
    ep128emuGUIEnvironment.Append(CPPPATH = ['./Fl_Native_File_Chooser'])
    ep128emuGLGUIEnvironment.Append(CPPPATH = ['./Fl_Native_File_Chooser'])
    makecfgEnvironment.Append(CPPPATH = ['./Fl_Native_File_Chooser'])
    tapeeditEnvironment.Append(CPPPATH = ['./Fl_Native_File_Chooser'])
if configure.CheckCHeader('stdint.h'):
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DHAVE_STDINT_H'])
if sys.platform[:5] == 'linux' and not mingwCrossCompile:
    if configure.CheckCHeader('linux/fd.h'):
        ep128emuLibEnvironment.Append(CCFLAGS = ['-DHAVE_LINUX_FD_H'])

oldLuaVersion = 0
if haveLua:
    if not configure.CheckType('lua_Integer',
                               '#include <lua.h>\n#include <lauxlib.h>'):
        oldLuaVersion = 1
        print('WARNING: using old Lua 5.0.x API')
configure.Finish()

if not havePortAudioV19:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DUSING_OLD_PORTAUDIO_API'])
if haveSDL:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DHAVE_SDL_H'])
if haveLua:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DHAVE_LUA_H'])
    if oldLuaVersion:
        ep128emuLibEnvironment.Append(CCFLAGS = ['-DUSING_OLD_LUA_API'])
if enableGLShaders:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DENABLE_GL_SHADERS'])
if not fltkVersion13:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DFLTK1'])
if cmosZ80:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DZ80_ENABLE_CMOS'])
if enableSDExt:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DENABLE_SDEXT'])
if enableReSID:
    ep128emuLibEnvironment.Append(CCFLAGS = ['-DENABLE_RESID'])

# Czo 096c820: preserve list-valued -arch pairs when propagating flags.
ep128emuGUIEnvironment.MergeFlags({'CCFLAGS': ep128emuLibEnvironment['CCFLAGS']})
ep128emuGLGUIEnvironment.MergeFlags({'CCFLAGS': ep128emuLibEnvironment['CCFLAGS']})
makecfgEnvironment.MergeFlags({'CCFLAGS': ep128emuLibEnvironment['CCFLAGS']})
tapeeditEnvironment.MergeFlags({'CCFLAGS': ep128emuLibEnvironment['CCFLAGS']})

def fluidCompile(flNames):
    cppNames = []
    # The FLUID emitter uses the global SCons Command builder, not the
    # per-target ENV: never rely on PATH to find the pinned Mac tool.
    fluidExecutable = ('"' + macPinnedFLTKFluid + '"') if hostMacBuild else 'fluid'
    # The Linux release pins FLUID explicitly: the global SCons Command
    # ignores caller PATH and otherwise picks Debian FLUID 1.3.8.
    if sys.platform[:5] == 'linux' and not mingwCrossCompile:
        linuxFluid = os.environ.get('EP128EMU_FLUID', '').strip()
        if linuxFluid:
            if not os.path.isfile(linuxFluid) or not os.access(linuxFluid, os.X_OK):
                Exit('LINUX_PINNED_FLUID_MISSING')
            fluidExecutable = '"' + linuxFluid + '"'
    for flName in flNames:
        if flName.endswith('.fl'):
            cppName = flName[:-3] + '_fl.cpp'
            hppName = flName[:-3] + '_fl.hpp'
            Command([cppName, hppName], flName,
                    '%s -c -o %s -h %s $SOURCES' % (fluidExecutable, cppName, hppName))
            cppNames += [cppName]
    return cppNames

ep128emuLibSources = Split('''
    src/bplist.cpp
    src/cfg_db.cpp
    src/compress.cpp
    src/comprlib.cpp
    src/debuglib.cpp
    src/decompm2.cpp
    src/display.cpp
    src/dotconf.c
    src/emucfg.cpp
    src/ep_fdd.cpp
    src/sdd_journal.cpp
    src/fileio.cpp
    src/fldisp.cpp
    src/gldisp.cpp
    src/guicolor.cpp
    src/joystick.cpp
    src/pngwrite.cpp
    src/script.cpp
    src/snd_conv.cpp
    src/soundio.cpp
    src/system.cpp
    src/tape.cpp
    src/videorec.cpp
    src/vm.cpp
    src/vmthread.cpp
    src/wd177x.cpp
''')
if enableMIDI:
    ep128emuLibSources += ['portmidi/pm_common/pmutil.c',
                           'portmidi/pm_common/portmidi.c',
                           'portmidi/porttime/porttime.c']
    if mingwCrossCompile:
        ep128emuLibSources += ['portmidi/pm_win/pmwin.c',
                               'portmidi/pm_win/pmwinmm.c',
                               'portmidi/porttime/ptwinmm.c']
    elif sys.platform[:5] == 'linux':
        ep128emuLibSources += ['portmidi/pm_linux/finddefault.c',
                               'portmidi/pm_linux/pmlinuxalsa.c',
                               'portmidi/pm_linux/pmlinux.c',
                               'portmidi/porttime/ptlinux.c']
    elif hostMacBuild:
        ep128emuLibSources += ['portmidi/pm_mac/finddefault.c',
                               'portmidi/pm_mac/pmmac.c',
                               'portmidi/pm_mac/pmmacosxcm.c',
                               'portmidi/pm_mac/readbinaryplist.c',
                               'portmidi/porttime/ptmacosx_mach.c']
if not fltkVersion13:
    ep128emuLibSources += ['Fl_Native_File_Chooser/Fl_Native_File_Chooser.cxx']
if disableOpenGL:
    ep128emuLibSources.remove('src/gldisp.cpp')
ep128emuLib = ep128emuLibEnvironment.StaticLibrary('ep128emu',
                                                   ep128emuLibSources)

# -----------------------------------------------------------------------------

ep128LibEnvironment = copyEnvironment(ep128emuLibEnvironment)
ep128LibEnvironment.Append(CPPPATH = ['./z80'])
sdextSources = []
if enableSDExt:
    sdextSources = ['src/sdext.cpp']

ep128Lib = ep128LibEnvironment.StaticLibrary('ep128', Split('''
    src/dave.cpp
    src/ep128vm.cpp
    src/ioports.cpp
    src/memory.cpp
    src/nick.cpp
    z80/z80.cpp
    z80/z80funcs2.cpp
    src/epmemcfg.cpp
    src/ide.cpp
    src/snapshot.cpp
''') + sdextSources)

# -----------------------------------------------------------------------------

zx128LibEnvironment = copyEnvironment(ep128emuLibEnvironment)
zx128LibEnvironment.Append(CPPPATH = ['./z80'])

zx128Lib = zx128LibEnvironment.StaticLibrary('zx128', Split('''
    src/ay3_8912.cpp
    src/zx128vm.cpp
    src/speccydos.cpp
    src/betadisk.cpp
    src/zxioport.cpp
    src/zxmemory.cpp
    src/ula.cpp
    src/zx_snap.cpp
'''))

# -----------------------------------------------------------------------------

cpc464LibEnvironment = copyEnvironment(ep128emuLibEnvironment)
cpc464LibEnvironment.Append(CPPPATH = ['./z80'])

cpc464Lib = cpc464LibEnvironment.StaticLibrary('cpc464', Split('''
    src/cpc464vm.cpp
    src/cpcio.cpp
    src/cpcmem.cpp
    src/crtc6845.cpp
    src/cpcvideo.cpp
    src/fdc765.cpp
    src/cpcdisk.cpp
    src/cpc_snap.cpp
'''))

# -----------------------------------------------------------------------------

tvc64LibEnvironment = copyEnvironment(ep128emuLibEnvironment)
tvc64LibEnvironment.Append(CPPPATH = ['./z80'])

tvc64Lib = tvc64LibEnvironment.StaticLibrary('tvc64', Split('''
    src/tvc64vm.cpp
    src/tvcmem.cpp
    src/tvcvideo.cpp
    src/tvc_snap.cpp
'''))

# -----------------------------------------------------------------------------

if enableReSID:
    residLibEnvironment = copyEnvironment(ep128emuLibEnvironment)
    residLibEnvironment.Append(CPPPATH = ['./resid'])
    residLibSources = Split('''
        resid/dac.cpp
        resid/envelope.cpp
        resid/extfilt.cpp
        resid/filter.cpp
        resid/pot.cpp
        resid/sid.cpp
        resid/version.cpp
        resid/voice.cpp
        resid/wave.cpp
    ''')
    residLib = residLibEnvironment.StaticLibrary('resid', residLibSources)

# -----------------------------------------------------------------------------

ep128emuEnvironment = copyEnvironment(ep128emuGLGUIEnvironment)
# Label the *binary variant*, not the CPU slice currently executing.
if hostMacBuild and sorted(userArch.split()) == ['arm64', 'x86_64']:
    ep128emuEnvironment.Append(CPPDEFINES = ['EP128EMU_MACOS_UNIVERSAL_BUILD'])
ep128emuEnvironment.Append(CPPPATH = ['./z80', './gui'])
if haveLua and oldLuaVersion:
    ep128emuEnvironment.Append(LIBS = ['lualib'])
ep128emuEnvironment.Prepend(LIBS = [ep128emuLib])
if enableReSID:
    ep128emuEnvironment.Prepend(LIBS = [residLib])
ep128emuEnvironment.Prepend(LIBS = [ep128Lib, zx128Lib, cpc464Lib, tvc64Lib])

ep128emuSources = ['gui/gui.cpp', 'gui/shortcut_cfg.cpp', 'gui/archive_drop.cpp']
if mingwCrossCompile:
    ep128emuSources += ['gui/win_archive_extract.cpp']
    ep128emuSources += ['gui/win_native_drop.cpp']
    ep128emuSources += ['gui/win_rar_fallback.cpp']
    ep128emuEnvironment.Append(LIBS = ['archive'])
if sys.platform[:5] == 'linux' and not mingwCrossCompile:
    ep128emuEnvironment.Append(LIBS = ['archive'])
if hostMacBuild:
    ep128emuSources += ['gui/macos_activity.mm', 'gui/macos_about.mm']
ep128emuSources += fluidCompile(['gui/gui.fl', 'gui/disk_cfg.fl',
                                 'gui/disp_cfg.fl', 'gui/kbd_cfg.fl',
                                 'gui/snd_cfg.fl', 'gui/vm_cfg.fl',
                                 'gui/debug.fl', 'gui/about.fl'])
ep128emuSources += ['gui/debugger.cpp', 'gui/monitor.cpp', 'gui/main.cpp']
if mingwCrossCompile:
    ep128emuResourceObject = ep128emuEnvironment.Command(
        'resource/resource.o',
        ['resource/ep128emu.rc', 'resource/cpc464emu.ico',
         'resource/ep128emu.ico', 'resource/tvc64emu.ico',
         'resource/zx128emu.ico'],
        toolNamePrefix + 'windres -v -o $TARGET resource/ep128emu.rc')
    ep128emuSources += [ep128emuResourceObject]
ep128emu = ep128emuEnvironment.Program('ep128emu', ep128emuSources)

if hostMacBuild:
    Command('ep128emu.app/Contents/MacOS/ep128emu', 'ep128emu',
            'mkdir -p ep128emu.app/Contents/MacOS ; cp -pf $SOURCES $TARGET')
    Command('ep128emu.app/Contents/Resources/EP_rele.wav',
            'resource/EP_rele.wav',
            'mkdir -p ep128emu.app/Contents/Resources ; cp -pf $SOURCES $TARGET')
    # Keep the original per-machine icon artwork with every macOS .app.
    # The Enterprise app/Finder icon remains the existing ep128emu.icns.
    for machineIcon in ('zx128emu', 'cpc464emu', 'tvc64emu'):
        Command('ep128emu.app/Contents/Resources/' + machineIcon + '.png',
                'resource/' + machineIcon + '.png',
                'mkdir -p ep128emu.app/Contents/Resources ; cp -pf $SOURCES $TARGET')

# -----------------------------------------------------------------------------

tapeeditEnvironment.Append(CPPPATH = ['./tapeutil'])
tapeeditEnvironment.Prepend(LIBS = ['ep128emu'])
tapeeditSources = fluidCompile(['tapeutil/tapeedit.fl'])
tapeeditSources += ['tapeutil/tapeio.cpp']
if mingwCrossCompile:
    tapeeditResourceObject = tapeeditEnvironment.Command(
        'resource/te_resrc.o',
        ['resource/tapeedit.rc', 'resource/tapeedit.ico'],
        toolNamePrefix + 'windres -v -o $TARGET resource/tapeedit.rc')
    tapeeditSources += [tapeeditResourceObject]
tapeedit = tapeeditEnvironment.Program('tapeedit', tapeeditSources)
Depends(tapeedit, ep128emuLib)

if hostMacBuild:
    Command('ep128emu.app/Contents/MacOS/tapeedit', 'tapeedit',
            'mkdir -p ep128emu.app/Contents/MacOS ; cp -pf $SOURCES $TARGET')

# -----------------------------------------------------------------------------

if buildUtilities:
    compressLibEnvironment = copyEnvironment(ep128emuLibEnvironment)
    compressLibEnvironment.Append(CPPPATH = ['./util/epcompress/src'])
    compressLibEnvironment.Prepend(LIBS = [ep128emuLib])
    compressLib = compressLibEnvironment.StaticLibrary(
                      'epcompress', Split('''
                          util/epcompress/src/archive.cpp
                          util/epcompress/src/compress3.cpp
                          util/epcompress/src/compress.cpp
                          util/epcompress/src/decompress3.cpp
                          util/epcompress/src/sfxcode.cpp
                          util/epcompress/src/sfxdecomp.cpp
                      '''))
    Depends(compressLib, ep128emuLib)
    epcompressEnvironment = copyEnvironment(compressLibEnvironment)
    epcompressEnvironment.Prepend(LIBS = [compressLib])
    if not mingwCrossCompile:
        epcompressEnvironment.Append(LIBS = ['pthread'])
    epcompress = epcompressEnvironment.Program(
                     'epcompress', ['util/epcompress/src/main.cpp'])
    Depends(epcompress, compressLib)
    iview2png = epcompressEnvironment.Program(
                    'iview2png', ['util/epimgconv/src/iview2png.cpp'])
    Depends(iview2png, compressLib)
    epimgconvEnvironment = copyEnvironment(ep128emuGLGUIEnvironment)
    epimgconvEnvironment.Append(CPPPATH = ['./util/epcompress/src'])
    epimgconvLib = epimgconvEnvironment.StaticLibrary(
                       'epimgconv', Split('''
                           util/epimgconv/src/attr16.cpp
                           util/epimgconv/src/epimgconv.cpp
                           util/epimgconv/src/imageconv.cpp
                           util/epimgconv/src/img_cfg.cpp
                           util/epimgconv/src/imgwrite.cpp
                           util/epimgconv/src/pixel16_1.cpp
                           util/epimgconv/src/pixel16_2.cpp
                           util/epimgconv/src/pixel256.cpp
                           util/epimgconv/src/pixel2.cpp
                           util/epimgconv/src/pixel4.cpp
                           util/epimgconv/src/tvc_16.cpp
                           util/epimgconv/src/tvc_2.cpp
                           util/epimgconv/src/tvc_4.cpp
                       '''))
    epimgconvEnvironment.Prepend(LIBS = [epimgconvLib, compressLib,
                                         ep128Lib, ep128emuLib])
    if mingwCrossCompile and not disableOpenGL:
        epimgconvCLIEnvironment = copyEnvironment(epimgconvEnvironment)
        epimgconvCLIEnvironment['LINKFLAGS'].remove('-mwindows')
        epimgconvCLIEnvironment.Append(CCFLAGS = ['-DDISABLE_OPENGL_DISPLAY'])
        epimgconvCLIMain = epimgconvCLIEnvironment.Object(
                               'main_cli', 'util/epimgconv/src/main.cpp')
        epimgconvCLI = epimgconvCLIEnvironment.Program(
                           'epimgconv', [epimgconvCLIMain])
        Depends(epimgconvCLI, epimgconvLib)
        Depends(epimgconvCLI, compressLib)
        Depends(epimgconvCLI, ep128emuLib)
    epimgconvSources = ['util/epimgconv/src/main.cpp']
    if not disableOpenGL:
        epimgconvSources += ['util/epimgconv/src/imgconvgui.cpp',
                             'util/epimgconv/src/img_disp.cpp',
                             fluidCompile(['util/epimgconv/src/epimgconv.fl'])]
    elif mingwCrossCompile:
        epimgconvEnvironment['LINKFLAGS'].remove('-mwindows')
    if mingwCrossCompile and not disableOpenGL:
        epimgconv = epimgconvEnvironment.Program('epimgconv_gui',
                                                 epimgconvSources)
    else:
        epimgconv = epimgconvEnvironment.Program('epimgconv', epimgconvSources)
    Depends(epimgconv, epimgconvLib)
    Depends(epimgconv, compressLib)
    Depends(epimgconv, ep128Lib)
    Depends(epimgconv, ep128emuLib)

# -----------------------------------------------------------------------------

makecfgEnvironment.Append(CPPPATH = ['./installer'])
makecfgEnvironment.Prepend(LIBS = ['ep128emu'])
if mingwCrossCompile:
    # WinINet is available since Win7; avoid distributing a libcurl DLL.
    makecfgEnvironment.Append(LIBS = ['wininet'])
if enableCURL:
    if configurePackage(makecfgEnvironment, 'cURL'):
        makecfgEnvironment.Append(CCFLAGS = ['-DMAKECFG_USE_CURL'])
if not mingwCrossCompile:
    if sys.platform[:5] == 'linux':
        for envName in ['DISPLAY', 'XAUTHORITY']:
            if envName in os.environ:
                makecfgEnvironment.Append(ENV = {
                                              envName : os.environ[envName] })

makecfg = makecfgEnvironment.Program(programNamePrefix + 'makecfg',
    ['installer/makecfg.cpp'] + fluidCompile(['installer/mkcfg.fl']))
Depends(makecfg, ep128emuLib)

if hostMacBuild:
    Command('ep128emu.app/Contents/MacOS/' + programNamePrefix + 'makecfg',
            programNamePrefix + 'makecfg',
            'mkdir -p ep128emu.app/Contents/MacOS ; cp -pf $SOURCES $TARGET')

# -----------------------------------------------------------------------------

if not mingwCrossCompile:
    makecfgEnvironment.Install(instBinDir,
                               [ep128emu, tapeedit, makecfg])
    for prgName in [instBinDir + "/zx128emu", instBinDir + "/cpc464emu",
                    instBinDir + "/tvc64emu"]:
        makecfgEnvironment.Command(prgName, ep128emu,
                                   ['ln -s -f ep128emu "' + prgName + '"'])
    if buildUtilities:
        makecfgEnvironment.Install(instBinDir,
                                   [epcompress, epimgconv, iview2png])
    makecfgEnvironment.Install(instPixmapDir,
                               ["resource/cpc464emu.png",
                                "resource/ep128emu.png",
                                "resource/tapeedit.png",
                                "resource/tvc64emu.png",
                                "resource/zx128emu.png"])
    makecfgEnvironment.Install(instDesktopDir,
                               ["resource/cpc464emu.desktop",
                                "resource/ep128.desktop",
                                "resource/eptapeedit.desktop",
                                "resource/tvc64emu.desktop",
                                "resource/zx128.desktop"])
    if not buildingLinuxPackage:
        confFileList = [instConfDir + '/EP_Keyboard_HU.cfg',
                        instConfDir + '/EP_Keyboard_US.cfg']
        confFiles = 0
        f = open("./installer/makecfg.cpp")
        for l in f:
          if not confFiles:
            confFiles = "machineConfigs" in l
          elif "};" in l:
            confFiles = None
            break
          elif '"' in l:
            confFileList += [instConfDir + '/'
                             + l[l.find('"') + 1:l.rfind('"')]]
        f.close()
        f = None
        makecfgEnvironment.Command(
            [instROMDir] + [confFileList], [makecfg],
            ['./' + programNamePrefix + 'makecfg -'
             + ['f "', 'c "'][int('DISPLAY' in makecfgEnvironment['ENV'])]
             + instDataDir + '"'])
    else:
        makecfgEnvironment.Command(instROMDir, None, [])
    makecfgEnvironment.Install(instConfDir,
                               ["config/clearkbd.cfg", "config/ep_keys.cfg"])
    makecfgEnvironment.Install(instDiskDir,
                               ["disk/disk.zip", "disk/ide126m.vhd.bz2",
                                "disk/ide189m.vhd.bz2"])
    makecfgEnvironment.Alias("install",
                             [instBinDir, instPixmapDir, instDesktopDir,
                              instDataDir, instROMDir, instConfDir,
                              instDiskDir])

