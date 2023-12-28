{ pkgs ? import <nixpkgs> {} }:

let
  clang = pkgs.llvmPackages_14.clang-unwrapped;
  # Change the version number here if we upgrade Ruby
  ruby = pkgs.ruby_3_2;

in pkgs.mkShell {
  nativeBuildInputs = with pkgs.buildPackages; [
    ruby
    clang
    openssl
    curl
    git
    which
    gdb
    jq
  ];

  shellHook = ''
    includebase="${ruby}/include"
    rubyinclude="$includebase/$(ls $includebase | head -n1)"
    platforminclude="$rubyinclude/$(ls $rubyinclude | grep -v ruby | head -n1)"

    clangbase="${pkgs.lib.makeLibraryPath [ clang ]}/clang"
    clanginclude="$clangbase/$(ls $clangbase | head -n1)/include"

    cat << YML > .clangd
CompileFlags:
  Add:
    - '-I$rubyinclude'
    - '-I$platforminclude'
    - '-I$clanginclude'
    - '-I${pkgs.glibc.dev}/include'
YML
  '';
}
