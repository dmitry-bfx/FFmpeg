#!/bin/bash
set -e

mkdir -p builds/intermediate_arm64
cd builds/intermediate_arm64
if [ "$1" = "configure" ]; then
  #make distclean
  ../../cpop-config.sh arm64 ../.. || exit 255
fi
make -j$(sysctl -n hw.ncpu) || exit 255
make install
cd ../..

mkdir -p builds/intermediate_x64
cd builds/intermediate_x64
if [ "$1" = "configure" ]; then
  #make distclean
  ../../cpop-config.sh x86_64 ../.. || exit 255
fi
make -j$(sysctl -n hw.ncpu) || exit 255
make install
cd ../..

ARM64_DIR="builds/intermediate_arm64/builds/arm64/lib"
X86_64_DIR="builds/intermediate_x64/builds/x86_64/lib"
UNIVERSAL_DIR="builds/universal/lib"

# Fix rpaths
fix_dylib_rpaths() {
  local DIR="$1"

  echo "Fixing rpaths in " $DIR

  if [ ! -d "$DIR" ]; then
    echo "Directory not found: $DIR"
    return 1
  fi
  
  find "$DIR" -type f -name '*.dylib' | while read -r dylib; do

    if [[ -L "$dylib" ]]; then
      echo "Skipping symlink: $dylib"
      continue
    fi
    
    local libname=$(basename "$dylib")
    
    local libdir=$(dirname "$dylib")

    echo "Fixing id for $libname"
    install_name_tool -id "@rpath/$libname" "$dylib"

    # Get dependencies of this dylib
    local deps=$(otool -L "$dylib" | tail -n +2 | awk '{print $1}')

    for dep in $deps; do
      # We want to fix only dependencies that are dylibs in the same DIR
      local depname=$(basename "$dep")

      if [ -f "$DIR/$depname" ]; then
        echo "  Changing dependency $dep -> @rpath/$depname"
        install_name_tool -change "$dep" "@rpath/$depname" "$dylib"
      fi
      
      if [[ "$dep" == /usr/local/opt/* || "$dep" == /opt/homebrew/opt/* ]]; then
        echo "  Found external dependency $dep"

        # Determine new path where we will place the copied dependency
        local new_dep_path="$libdir/$depname"

        # Copy only if not already copied
        if [ ! -f "$new_dep_path" ]; then
          echo "  Copying $dep to $new_dep_path"
          cp -P "$dep" "$new_dep_path"
          chmod +w "$new_dep_path"
          echo "  Setting @rpath ID for copied $depname"
          install_name_tool -id "@rpath/$depname" "$new_dep_path"
        fi

        # Change this dylib's reference to the copied local version
        echo "  Rewriting dependency $dep -> @rpath/$depname"
        install_name_tool -change "$dep" "@rpath/$depname" "$dylib"
      fi
    done
  done
}


fix_dylib_rpaths $ARM64_DIR
fix_dylib_rpaths $X86_64_DIR


# Make fat binaries
rm -rf "$UNIVERSAL_DIR"
mkdir -p "$UNIVERSAL_DIR"

for arch_file in "$ARM64_DIR"/*.dylib "$X86_64_DIR"/*.dylib; do

  fname=$(basename "$arch_file")
  libdir=$(dirname "$arch_file")
    
  arm64_path="$ARM64_DIR/$fname"
  x86_64_path="$X86_64_DIR/$fname"
  universal_path="$UNIVERSAL_DIR/$fname"

  # Skip symlinks
  if [[ -L "$arch_file" ]]; then
    echo "Copying symlink: $arch_file"
    cp -P "$arch_file" "$universal_path"
    continue
  fi

  # Only process each file once
  [[ -f "$universal_path" ]] && continue

  if [[ -f "$arm64_path" && -f "$x86_64_path" ]]; then
    echo "Creating fat binary: $fname"
    lipo -create "$arm64_path" "$x86_64_path" -output "$universal_path"
  elif [[ -f "$arm64_path" ]]; then
    echo "Only arm64 exists, copying: $fname"
    cp "$arm64_path" "$universal_path"
  elif [[ -f "$x86_64_path" ]]; then
    echo "Only x86_64 exists, copying: $fname"
    cp "$x86_64_path" "$universal_path"
  else
    echo "Missing both architectures for: $fname"
  fi
done

