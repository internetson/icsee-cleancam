from elftools.elf.elffile import ELFFile

with open('extracted/romfs-x/lib/libXmDvr.so', 'rb') as f:
    elf = ELFFile(f)
    symtab = elf.get_section_by_name('.dynsym')
    for sym in symtab.iter_symbols():
        if 'Reset' in sym.name or 'Frontboard' in sym.name or 'ResetKey' in sym.name:
            print(sym.name, hex(sym['st_value']), sym['st_size'])
