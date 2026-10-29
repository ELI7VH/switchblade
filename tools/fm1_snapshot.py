#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Archive current music, installed package and all setlist storage without flashing.
Raw flash is recovery evidence: never write it wholesale onto a different unit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time
import zlib

FLASH_LO, FLASH_HI = 0x93000, 0x100000
OBJECTS = [0, 1, 2, 3, 4, 5, 6, 9] + list(range(32, 44))

def sectors(obj, copy):
    if obj >= 32: base = 0xA0000 + (obj-32)*8192 + copy*4096
    elif obj == 0: base = 0xFC000 + copy*4096
    elif obj == 9: base = 0xE5000 + copy*4096
    elif obj >= 5: base = 0xDC000 + (obj-5)*8192 + copy*4096
    else: base = 0x97000 + (obj-1)*8192 + copy*4096
    extended = 1 <= obj <= 4 or obj == 9 or 32 <= obj <= 43
    idx = 5+obj-32 if obj>=32 else 4 if obj==9 else obj-1
    return base, 0xB8000+(idx*2+copy)*4096 if extended else None

def decode_object(raw, obj):
    def get(off, n):
        offset = off-FLASH_LO
        if offset<0 or offset+n>len(raw): raise ValueError('incomplete flash snapshot')
        return raw[offset:offset+n]
    copies=[]
    for copy in range(2):
        base, extra = sectors(obj,copy)
        hdr = get(base,32)
        magic,typ,slot,seq,length,crc,_,_,hcrc=struct.unpack('<IHH6I',hdr)
        if magic!=0x554C4546 or typ!=obj or slot!=copy:continue
        if length>(7936 if extra is not None else 3840):continue
        if zlib.crc32(hdr[:28])&0xffffffff!=hcrc:continue
        payload=get(base+256,min(length,3840))
        if length>3840: payload+=get(extra,length-3840)
        if zlib.crc32(payload)&0xffffffff!=crc:continue
        copies.append({'copy':copy,'generation':seq,'length':length,'crc32':f'{crc:08x}','payload':payload})
    if not copies:return None
    a=copies[0]
    if len(copies)==2:
        b=copies[1]
        if b['generation']!=a['generation'] and ((b['generation']-a['generation'])&0xffffffff)<0x80000000:a=b
    return a

def flash_query(port, off, length=256):
    port.write(f'flr 0x{off:x} {length}\n'.encode())
    data=bytearray();end=time.monotonic()+5
    while time.monotonic()<end:
        data+=port.read(port.in_waiting or 1)
        if data.endswith(b'> '):
            rows=re.findall(rb'([0-9A-Fa-f]{6}):((?: [0-9A-Fa-f]{2}){1,16})',data)
            result=bytearray()
            for address, body in rows:
                if int(address,16)!=off+len(result):raise ValueError('flash reply address mismatch')
                result+=bytes.fromhex(body.decode())
            if len(result)!=length:raise ValueError(f'short flash reply at {off:x}: {len(result)}')
            return bytes(result)
    raise TimeoutError(f'console timeout at {off:x}')

def capture_flash(port, dev):
    chunks=[];ping=time.monotonic();report=ping
    for off in range(FLASH_LO,FLASH_HI,256):
        chunks.append(flash_query(port,off))
        now=time.monotonic()
        if now-ping>=2:
            r=dev.ask(65,[]) # hold autosave while making the read-only storage snapshot
            if r[1]:raise RuntimeError('backup session refused during flash capture')
            ping=time.monotonic()
        if now-report>=15:
            print(f'Flash read {(off-FLASH_LO)*100//(FLASH_HI-FLASH_LO)}%',flush=True);report=now
    return b''.join(chunks)

def main():
    import serial
    import mido
    from fm1_song import FM1,backup
    import shutil
    import zipfile
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--serial-port',required=True)
    ap.add_argument('--firmware',type=Path,required=True)
    ap.add_argument('--install-receipt',type=Path,required=True)
    args=ap.parse_args()
    out=args.output.expanduser().resolve()
    if out.exists():raise SystemExit('Choose a new archive directory')
    receipt=json.loads(args.install_receipt.read_text())
    firmware=args.firmware.read_bytes()
    if hashlib.sha256(firmware).hexdigest()!=receipt['firmware_sha256']:raise SystemExit('Installed package checksum mismatch')
    ports=[p for p in mido.get_input_names() if re.search('fm-1|felucca|switch|composite|usb-midi',p,re.I)]
    if len(ports)!=1:raise SystemExit(f'Connect only the source FM-1; matching input ports: {ports}')
    dev=FM1();info=dev.ask(1,[]);name=bytes(info[:info.index(0)]).decode()
    if name!=receipt['installed_info']:raise SystemExit(f'Unexpected source firmware: {name}')
    clock=dev.ask(2,[1,27])
    if (clock[2]|clock[3]<<7)-8192!=1:raise SystemExit('Enable CLOCK OUT so stopped transport can be verified')
    end=time.monotonic()+2
    while time.monotonic()<end:
        for msg in dev.i.iter_pending():
            if msg.type in ('clock','start','continue','note_on'):raise SystemExit('Stop playback before the snapshot')
        time.sleep(.005)
    out.mkdir(parents=True);(out/'native-objects').mkdir()
    backup(dev,out/'logical')
    (out/'live-master-state.json').write_text(json.dumps({'editor_command':82,'payload':dev.ask(82,[])},indent=2)+'\n')
    print('Reading complete storage, including twelve setlist slots',flush=True)
    with serial.Serial(args.serial_port,115200,timeout=.05) as port:
        time.sleep(.2);port.reset_input_buffer()
        raw=capture_flash(port,dev)
        (out/'storage-093000-0fffff.bin').write_bytes(raw)
        print('Reading storage again for byte-for-byte verification',flush=True)
        again=capture_flash(port,dev)
        if raw!=again:raise RuntimeError('Storage changed between reads; partial archive retained, not marked complete')
    objects=[]
    for obj in OBJECTS:
        record=decode_object(raw,obj)
        if record is None:objects.append({'native_object':obj,'state':'no valid copy'});continue
        payload=record.pop('payload');record['native_object']=obj
        record['file']=f'native-objects/obj{obj:03d}.bin'
        (out/record['file']).write_bytes(payload)
        if obj>=32:record['song_slot']=obj-31
        objects.append(record)
    shutil.copy2(args.firmware,out/args.firmware.name)
    shutil.copy2(args.install_receipt,out/'installed-build-receipt.json')
    controller=Path('controllers/fm1-sb-live.minilab3')
    if controller.exists():shutil.copy2(controller,out/controller.name)
    commit=receipt['source_commit']
    subprocess.run(['git','archive','--format=tar.gz',f'--output={out / "source.tar.gz"}',commit],check=True)
    shutil.copy2(Path(__file__),out/'fm1_snapshot.py')
    (out/'README.txt').write_text(
        'FM1-TEST snapshot for transfer to FM1-SHOW.\n'
        'Firmware and logical/obj0.bin capture the installed build and current music.\n'
        'logical/ also holds current settings, project slots and preset banks (CRC-verified).\n'
        'native-objects/ preserves all CRC-valid stored objects, including twelve setlist slots.\n'
        'The raw storage image was read twice and matched exactly. It is recovery evidence.\n'
        'Do not clone raw device regions onto another unit: use validated musical-object restore.\n'
        'Back up and identify FM1-SHOW before writing anything. Never flash FM1-GHOUL.\n'
        'The running logical/obj0.bin is authoritative if newer than its stored song slot.\n'
        'live-master-state.json preserves read-only master-strip control state.\n'
        'Current editor restore supports only logical IDs 0..7. Setlist exports are retained\n'
        'for a deliberate setlist restore path; do not invent backup IDs for them.\n')
    hashes={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob('*')) if p.is_file()}
    manifest={'complete':True,'unit':'FM1-TEST','intended_target':'FM1-SHOW','installed_info':name,
              'source_commit':commit,'firmware_sha256':receipt['firmware_sha256'],
              'current_song_slot':(out/'logical/obj0.bin').read_bytes()[67],
              'current_runtime_authoritative':True,
              'raw_range':[FLASH_LO,FLASH_HI],'raw_double_read_equal':True,'native_objects':objects,'sha256':hashes}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out.parent))
    print('COMPLETE',out,flush=True)
    print('ZIP',archive,flush=True)
    print('Stored songs',sum(o.get('song_slot',0)>0 and o.get('length',0)>0 for o in objects),flush=True)
if __name__=='__main__':main()
