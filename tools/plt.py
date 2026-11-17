from elftools.elf.elffile import ELFFile

with open('extracted/romfs-x/lib/libXmDvr.so', 'rb') as f:
    elf = ELFFile(f)
    rel = elf.get_section_by_name('.rel.plt')
    dynsym = elf.get_section_by_name('.dynsym')
    plt = elf.get_section_by_name('.plt')
    plt_addr = plt['sh_addr']
    print("PLT addr:", hex(plt_addr))
    
    # Each PLT entry is 12 or 16 bytes on ARM
    # In ARM Linux, PLT entry 0 is 20 bytes, followed by 12 bytes each
    # Let's list relocations
    entries = []
    for r in rel.iter_relocations():
        sym = dynsym.get_symbol(r['r_info_sym'])
        entries.append((r['r_offset'], sym.name))
        
    for i, (off, name) in enumerate(entries):
        addr = plt_addr + 20 + i * 12
        print(f"{hex(addr)} (got {hex(off)}): {name}")
