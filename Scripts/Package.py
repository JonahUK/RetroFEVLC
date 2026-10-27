import argparse
import errno
import glob
import os
import shutil
import sys

def copytree(src, dst):
  if os.path.isdir(src):
      if not os.path.exists(dst):
          mkdir_p(dst)
      for name in os.listdir(src):
          copytree(os.path.join(src, name),
                   os.path.join(dst, name))
  else:
      print("COPY: " + dst)
      shutil.copyfile(src, dst)
      
def mkdir_p(path):
  print("CREATE: " + path)
  try:
    os.makedirs(path)
    
  except OSError as exception:
    if exception.errno != errno.EEXIST:
      raise
#todo: this script needs to be broken up into multiple methods 
#      and should be ported to be more like a class

#####################################################################
# Parse arguments
#####################################################################
parser = argparse.ArgumentParser(description='Bundle up some RetroFE common files.')
parser.add_argument('--os', choices=['windows','linux','mac'], required=True, help='Operating System (windows or linux or mac)')
parser.add_argument('--build', default='full', help='Define what contents to package (full, core, engine, layout, none')
parser.add_argument('--clean', action='store_true', help='Clean the output directory')
parser.add_argument('--compiler', help='Compiler to use (vs, mingw, gcc')

parser.add_argument('--vlc-x64', action='store_true', help='Package the Windows x64 LibVLC build')
parser.add_argument('--build-dir', help='CMake build directory (e.g. RetroFE/Build-Search-x64); default: Build-Search-x64 when present, otherwise Build-VLC-x64')
parser.add_argument('--runtime-core', help='Optional existing known-good x64 core directory used only to supply missing DLLs')
args = parser.parse_args()

#####################################################################
# Determine base path os to build
#####################################################################
base_path = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
common_path = os.path.join(base_path, 'Package', 'Environment', 'Common')
os_path = None

if args.os == 'windows':
  os_path = os.path.join(base_path, 'Package', 'Environment', 'Windows')
  
elif args.os == 'linux':
  os_path = os.path.join(base_path, 'Package', 'Environment', 'Linux')

elif args.os == 'mac':
  os_path = os.path.join(base_path, 'Package', 'Environment', 'MacOS')

#####################################################################
# Copy layers, artwork, config files, etc for the given os
#####################################################################
output_path = os.path.join(base_path, 'Artifacts', args.os, 'RetroFE')

if os.path.exists(output_path) and args.clean:
  # Preserve existing collections, media and settings. --clean now refreshes
  # package files in place; it never destroys a populated RetroFE installation.
  print('SAFE CLEAN: preserving existing runtime, collections, media and settings')


if args.build != 'none' and not os.path.exists(output_path):
  os.makedirs(output_path)

if args.build == 'full':
  collection_path = os.path.join(output_path, 'collections')
  copytree(common_path, output_path)
  if args.os == 'windows' and args.vlc_x64:
    for name in os.listdir(os_path):
      if name.lower() != 'core':
        copytree(os.path.join(os_path, name),
                 os.path.join(output_path, name))
  else:
    copytree(os_path, output_path)
  
  mkdir_p(os.path.join(output_path, 'meta', 'mamelist'))
  
  dirs = [d for d in os.listdir(collection_path) if os.path.isdir(os.path.join(collection_path, d))]
  for collection in dirs:
    if not collection.startswith('_'):
     mkdir_p(os.path.join(output_path, 'collections', collection, 'roms'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'artwork_front'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'artwork_back'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'medium_back'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'medium_front'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'bezel'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'logo'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'screenshot'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'screentitle'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'medium_artwork', 'video'))
     mkdir_p(os.path.join(output_path, 'collections', collection, 'system_artwork'))
 
elif args.build == 'layout':
  layout_dest_path = os.path.join(output_path, 'layouts')
  layout_common_path = os.path.join(common_path, 'layouts')
  layout_os_path = os.path.join(os_path, 'layouts')
  
  if not os.path.exists(layout_dest_path):
    os.makedirs(layout_dest_path)

  if os.path.exists(layout_common_path):
    copytree(layout_common_path, layout_dest_path)
    
  if os.path.exists(layout_os_path):
    copytree(layout_os_path, layout_dest_path)

#####################################################################
# Copy retrofe executable
#####################################################################
if args.os == 'windows':
  if args.build == 'full' or args.build == 'core' or args.build == 'engine':
    # copy retrofe.exe to core folder
    if(hasattr(args, 'compiler') and args.compiler == 'mingw'):
      src_exe = os.path.join(base_path, 'RetroFE', 'Build', 'retrofe.exe')
    else:
      src_exe = os.path.join(base_path, 'RetroFE', 'Build', 'Release', 'retrofe.exe')
      
    if args.vlc_x64:
      build_dir = args.build_dir
      if not build_dir:
        candidates = [os.path.join(base_path, 'RetroFE', 'Build-Search-x64'),
                      os.path.join(base_path, 'RetroFE', 'Build-VLC-x64')]
        build_dir = next((d for d in candidates if os.path.isfile(os.path.join(d, 'Release', 'retrofe.exe'))), candidates[0])
      src_exe = os.path.join(os.path.abspath(build_dir), 'Release', 'retrofe.exe')
      if not os.path.isfile(src_exe):
        raise RuntimeError('Built x64 executable missing: ' + src_exe)

    core_path = os.path.join(output_path, 'core')
    
    # create the core folder
    if not os.path.exists(core_path):
      os.makedirs(core_path)
      
    # copy retrofe.exe
    shutil.copy(src_exe, core_path)

    if args.vlc_x64:
      runtime_path = os.path.dirname(src_exe)

      def pe_machine(path):
        # Reject 32-bit DLLs (the original packaging failure).
        import struct
        try:
          with open(path, 'rb') as f:
            if f.read(2) != b'MZ':
              return None
            f.seek(0x3c)
            off = struct.unpack('<I', f.read(4))[0]
            f.seek(off)
            if f.read(4) != b'PE\0\0':
              return None
            return struct.unpack('<H', f.read(2))[0]
        except (OSError, struct.error):
          return None

      def stage_dll(path):
        machine = pe_machine(path)
        if machine != 0x8664:
          raise RuntimeError('Not an x64 Windows DLL: ' + path + ' (PE machine: ' + str(machine) + ')')
        shutil.copy2(path, os.path.join(core_path, os.path.basename(path)))
        print('x64 DLL: ' + os.path.basename(path) + ' <- ' + path)

      for dll in glob.glob(os.path.join(runtime_path, '*.dll')):
        stage_dll(dll)

      # Search only for specific required runtime libraries. Prefer explicitly
      # marked x64 folders; never silently choose an x86 build.
      # SDL2_image and SDL2_mixer depend on these additional codecs. Missing
      # codec DLLs can leave Aeon Nox artwork/UI elements blank even when the
      # executable, search and LibVLC video playback otherwise work.
      required = [
          'SDL2.dll', 'SDL2_image.dll', 'SDL2_mixer.dll',
          'SDL2_ttf.dll', 'libfreetype-6.dll', 'zlib1.dll',
          'libFLAC-8.dll', 'libjpeg-9.dll', 'libmodplug-1.dll',
          'libmpg123-0.dll', 'libogg-0.dll', 'libopus-0.dll',
          'libopusfile-0.dll', 'libpng16-16.dll', 'libtiff-5.dll',
          'libvorbis-0.dll', 'libvorbisfile-3.dll', 'libwebp-7.dll',
      ]
      thirdparty = os.path.join(base_path, 'RetroFE', 'ThirdParty')
      for name in required:
        dst = os.path.join(core_path, name)
        if os.path.isfile(dst) and pe_machine(dst) == 0x8664:
          continue
        if os.path.isfile(dst):
          os.remove(dst)  # Wrong-architecture DLL cannot be retained.
        matches = []
        for dirpath, _, filenames in os.walk(thirdparty):
          if name.lower() in (f.lower() for f in filenames):
            for filename in filenames:
              if filename.lower() == name.lower():
                path = os.path.join(dirpath, filename)
                if pe_machine(path) == 0x8664:
                  matches.append(path)
        def rank(path):
          normalized = path.replace('\\', '/').lower()
          is_explicit_x64 = any(part in ('x64', 'win64', 'amd64', 'zlib-x64', 'freetype-x64') for part in normalized.split('/'))
          return (not is_explicit_x64, len(path), path.lower())
        matches.sort(key=rank)
        if matches:
          stage_dll(matches[0])
        elif args.runtime_core and os.path.isfile(os.path.join(args.runtime_core, name)):
          stage_dll(os.path.join(args.runtime_core, name))
        else:
          raise RuntimeError('Missing x64 runtime DLL: ' + name +
                             '. Add it to ThirdParty or supply --runtime-core <known-good-core>.')

      plugins_src = os.path.join(runtime_path, 'plugins')
      plugins_dst = os.path.join(core_path, 'plugins')

      if not os.path.isdir(plugins_src):
        raise RuntimeError('VLC plugins directory not found: ' + plugins_src)

      shutil.copytree(plugins_src, plugins_dst, dirs_exist_ok=True)
#    third_party_path = os.path.join(base_path, 'RetroFE', 'ThirdParty')
        
elif args.os == 'linux':
  if args.build == 'full' or args.build == 'core' or args.build == 'engine':
    src_exe = os.path.join(base_path, 'RetroFE', 'Build', 'retrofe')
    shutil.copy(src_exe, output_path)

elif args.os == 'mac':
  if args.build == 'full' or args.build == 'core' or args.build == 'engine':
    src_exe = os.path.join(base_path, 'RetroFE', 'Build', 'retrofe')
    shutil.copy(src_exe, output_path)
    app_path = os.path.join(output_path, 'RetroFE.app')
    if not os.path.exists(app_path):
      copytree(os_path, output_path)
    shutil.copy(src_exe, output_path + '/RetroFE.app/Contents/MacOS/')





