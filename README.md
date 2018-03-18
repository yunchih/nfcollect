# nfcollect

Collect Netfilter NFLOG log entries and commit them to stable storage in binary (compressed) format.

The project contains two binaries: `nfcollect` and `nfextract`:

#### `nfcollect`

Collect packets from *Netfilter* netlink kernel interface.  Packets are
aggregated onto a memory region (we call it *a trunk*), until the *trunk* is full.
A full *trunk* will be committed to disk by configurable means (currently `zstd`
compression and no compression is implemented).  Trunks will be stored in a
specific directory, which will be scanned by `nfextract` to extract all trunks.

Due to communication with the kernel, **this program requires root privilege**.


## Build

```bash
./bootstrap.sh
./configure
make
```

Run `./configure --enable-debug` to enable debug output.

## Usage

``` bash
$ ./nfcollect --help
Usage: nfcollect [OPTION]

Options:
  -c --compression=<algo>      compression algorithm to use (default: no compression)
  -d --storage_dir=<dirname>   log files storage directory
  -h --help                    print this help
  -g --nflog-group=<id>        the group id to collect
  -p --parallelism=<num>       max number of committer thread
  -s --storage_size=<dirsize>  log files maximum total size in MiB
  -v --version                 print version information

$ ./nfextract -h     
Usage: nfextract [OPTION]

Options:
  -d --storage_dir=<dirname>   log files storage directory
  -h --help                    print this help
  -v --version                 print version information
```

#### Examples

```bash
# Send all packets destined for localhost to the nflog group #5
sudo iptables -A OUTPUT -p tcp -d 127.0.0.1  -j NFLOG --nflog-group 5

# Receive the packets from nfnetlink
mkdir my-nflog
sudo ./nfcollect -d my-nflog -g 5 -s 100 -c zstd

# Let it collect for a while ...

# Dump the collected packets
./nfextract -d my-nflog
```


### References

* libnetfilter_log: https://www.icir.org/gregor/tools/files/doc.libnetfilter_log/html/libnetfilter__log.html
* zstd: https://facebook.github.io/zstd/zstd_manual.html
* lz4: https://github.com/lz4/lz4
