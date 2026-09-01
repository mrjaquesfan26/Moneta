# Moneta

Designed for Linux, compatible with WSL.

## Installing Dependencies

Instructions for Arch:

```bash
yay -S --needed base-devel cmake pkgconf openssl zeromq libsodium unbound \
  libunwind xz readline expat libpgm qt5-tools hidapi libusb protobuf \
  systemd-libs boost boost-libs python ccache doxygen graphviz nettle libevent
```

## Building from Source

```bash
git clone <repo-url>
cd moneta
```

Build external dependencies:

```bash
cd external/monero-project
rm -r build
make release-static -j$(nproc)   # on WSL, use about half your processors to prevent crash
ln build/libmonero-cpp.so ../
cd ../../
```

Build Moneta:

```bash
mkdir build
cd build
cmake .. -DMONETA_TUI=ON        # OFF to build without the TUI
make -j$(nproc)                 # same WSL warning as above
```

## Usage

```
moneta usage:
  -w <wallet> <password>       wallet file
  -cw <wallet> <password>      create wallet
  -ca <label>                  create subaddress with label
  -a <range>                   address selection: 1 | 2:4 | 2,4
  -la                          list created subaddresses
  -b                           show balance
  -s <amount> <dest address>   send/spend monero
  -sc <amount> <name>          send to a saved contact
  -lc                          list contacts
  -tx <N>                      show tx history (0 for all)
  -c                           clean output (CSV output)
  -d <host:port>               daemon address
  -h, --help                   show help
  -cfg <path>                  config file
  -tui                         launch interactive TUI
```
