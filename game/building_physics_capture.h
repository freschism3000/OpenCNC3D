/* Script-only raw building-body proof. Hash explicit fields, never padding,
   pointers or vector storage. Normal gameplay neither calls nor allocates here. */
static void building_physics_capture() {
    uint64_t hash=1469598103934665603ull;
    uint64_t bodies=0;
    auto add=[&](const void* p,size_t n) {
        const unsigned char* b=(const unsigned char*)p;
        for(size_t i=0;i<n;i++){hash^=b[i];hash*=1099511628211ull;}
    };
    auto flag=[&](bool b){const unsigned char v=b?1:0;add(&v,sizeof v);};
    for(const auto& item:g_ebldBld) {
        const EbldBld& E=item.second;
        add(&item.first,sizeof item.first);add(E.type,strlen(E.type)+1);
        add(&E.model,sizeof E.model);add(&E.stage,sizeof E.stage);
        add(&E.blastTick,sizeof E.blastTick);add(&E.damagedTick,sizeof E.damagedTick);
        flag(E.sinking);flag(E.dropped);
        for(int which=0;which<2;which++) {
            const auto& V=which?E.wreck:E.cut;
            const uint64_t count=(uint64_t)V.size();add(&count,sizeof count);
            for(const auto& B:V) {
                add(B.p,sizeof B.p);add(B.pp,sizeof B.pp);
                add(B.q,sizeof B.q);add(B.pq,sizeof B.pq);
                add(B.v,sizeof B.v);add(B.w,sizeof B.w);
                add(&B.y0,sizeof B.y0);add(&B.still,sizeof B.still);
                add(&B.ghost,sizeof B.ghost);add(&B.tore,sizeof B.tore);
                flag(B.live);flag(B.asleep);flag(B.pinned);
                bodies++;
            }
        }
    }
    printf("BUILDINGPHYSICSCAPTURE|frame=%d|entries=%zu|body_records=%llu|hash=%016llx\n",
           g_engineFrame,g_ebldBld.size(),(unsigned long long)bodies,(unsigned long long)hash);
}
