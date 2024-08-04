## What is this?

DSRemote ported to work with Rigol DHO800 and DHO900 series. Currently at experimental stage.

Tested only with DHO924S via network (with wifi) on Debian 12.5 Linux/GNU.

## How to run from precompiled binary (Debian)

```bash
sudo apt-get update
sudo apt-get install git qt5-default
git clone https://github.com/norbertkiszka/DSRemote_DHO800_DHO900.git
cd DSRemote_DHO800_DHO900
./dsremote
```

## How to compile and install (Debian)

```bash
sudo apt-get update
sudo apt-get install g++ make git qtbase5-dev-tools qtbase5-dev qt5-default
git clone https://github.com/norbertkiszka/DSRemote_DHO800_DHO900.git
cd DSRemote_DHO800_DHO900
make -j4
sudo make install
dsremote
```


## Detailed oscilloscopes list for SEO:

- Rigol DHO924S
- Rigol DHO924
- Rigol DHO914S
- Rigol DHO814
- Rigol DHO812
- Rigol DHO804
- Rigol DHO802
