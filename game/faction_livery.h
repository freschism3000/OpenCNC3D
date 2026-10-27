/* Faction artwork is independent of the player's paint. FCLV0001 stores sparse,
   authored Gold-atlas/mask replacements, not a hue test or a newly drawn emblem.
   Three CRCs bind each entry to the exact approved GDI/Nod/mask texture bytes;
   a stale or malformed sidecar is ignored without changing the model or campaign. */
#ifndef FACTION_LIVERY_H
#define FACTION_LIVERY_H

static bool livery_faction_u32(FILE* f, uint32_t* value)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16)
           | ((uint32_t)b[3] << 24);
    return true;
}
static uint32_t livery_faction_crc(const std::vector<unsigned char>& v)
{
    return (uint32_t)crc32(0L, v.empty() ? Z_NULL : &v[0], (uInt)v.size());
}
static void livery_faction_load(PackTex& tx, const char* model, unsigned texture,
                               const std::vector<unsigned char>& gold,
                               const std::vector<unsigned char>& nod,
                               const std::vector<unsigned char>& mask)
{
    if (!tx.faction.empty()) return;
    const std::string path = std::string(model) + ".fclv";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    char magic[8]; uint32_t entries = 0;
    bool ok = fread(magic, 1, 8, f) == 8 && !memcmp(magic, "FCLV0001", 8)
           && livery_faction_u32(f, &entries) && entries > 0 && entries <= 64;
    std::vector<LiveryFactionTexel> result;
    for (uint32_t e = 0; ok && e < entries; e++) {
        uint32_t h[8];
        for (int i = 0; i < 8 && ok; i++) ok = livery_faction_u32(f, &h[i]);
        if (!ok) break;
        const uint32_t ti=h[0], w=h[1], height=h[2], count=h[6], size=h[7];
        if (!w || !height || w > 4096 || height > 4096 || !count
            || count > w*height || !size || size > 16u*1024u*1024u) { ok=false; break; }
        if (ti != texture) { if (fseek(f, size, SEEK_CUR)) ok=false; continue; }
        if (w != (unsigned)tx.w || height != (unsigned)tx.h
            || gold.size() != (size_t)w*height*4 || nod.size() != gold.size()
            || mask.size() != (size_t)w*height || h[3] != livery_faction_crc(gold)
            || h[4] != livery_faction_crc(nod) || h[5] != livery_faction_crc(mask)) {
            ok=false; break;
        }
        std::vector<unsigned char> z(size), raw((size_t)count*9);
        uLongf got=(uLongf)raw.size();
        ok=fread(&z[0],1,z.size(),f)==z.size()
            && uncompress(&raw[0],&got,&z[0],(uLong)z.size())==Z_OK && got==raw.size();
        if (!ok) break;
        result.reserve(count);
        for (uint32_t i=0; i<count; i++) {
            const unsigned char* p=&raw[(size_t)i*9];
            LiveryFactionTexel q;
            q.pixel=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
            if (q.pixel >= w*height || (!result.empty() && q.pixel <= result.back().pixel)
                || p[7] != gold[(size_t)q.pixel*4+3]) { ok=false; break; }
            memcpy(q.rgba,p+4,4);q.mask=p[8];result.push_back(q);
        }
        break;
    }
    fclose(f);
    if (ok && !result.empty()) {
        tx.faction.swap(result);
        fprintf(stderr,"TEAMCOLOUR|faction|texture=%u|patch-texels=%zu\n",texture,tx.faction.size());
    } else if (!ok) fprintf(stderr,"TEAMCOLOUR|faction|ignored incompatible %s texture=%u\n",path.c_str(),texture);
}
#endif
