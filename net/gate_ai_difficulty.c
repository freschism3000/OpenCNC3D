/* Fair skirmish difficulty's wire contract and consent permissions, with no window.
 * Include the transport implementation to exercise its actual byte codec and offer
 * comparison rather than constructing a second codec in a test. */
#include "netmatch.c"
#include <assert.h>

int main(void)
{
    NmSetup in, out, changed;
    unsigned char wire[NM_SETUP_BYTES];
    int seat, level;
    memset(&in, 0, sizeof in);
    snprintf(in.scenario, sizeof in.scenario, "%s", "SCM01EA");
    for (seat = 0; seat < NM_MAX_SEATS; seat++) {
        in.ai_levels[seat] = 1;
        snprintf(in.name[seat], sizeof in.name[seat], "Commander%d", seat);
    }
    for (seat = 0; seat < NM_MAX_SEATS; seat++) for (level = 0; level < 4; level++) {
        in.ai_levels[seat] = (unsigned char)level;
        assert(setup_pack(&in, wire) == 200);
        setup_unpack(&out, wire);
        assert(!memcmp(in.ai_levels, out.ai_levels, NM_MAX_SEATS));
        assert(!memcmp(in.name, out.name, sizeof in.name));
        assert(!lobby_offer_changed(&in, &out));
        changed = in;
        changed.ai_levels[seat] = (unsigned char)((level + 1) % 4);
        assert(lobby_offer_changed(&in, &changed));
    }
    wire[192 + 7] = 255;
    setup_unpack(&out, wire);
    assert(out.ai_levels[7] == 1);
    s_is_host = 1;
    s_setup = in;
    s_setup.seats = 3;
    s_setup.mode[0] = s_setup.mode[1] = NM_SEAT_HUMAN;
    s_setup.mode[2] = NM_SEAT_BOT;
    s_have[1] = 1;
    s_ready[1] = 1;
    changed = s_setup;
    changed.ai_levels[2] = (unsigned char)((s_setup.ai_levels[2] + 1) % 4);
    nm_lobby_set_setup(&changed);
    assert(!s_ready[1]);
    assert(s_setup.ai_levels[2] == changed.ai_levels[2]);
    s_is_host = 0;
    changed.ai_levels[2] = (unsigned char)((s_setup.ai_levels[2] + 1) % 4);
    nm_lobby_set_setup(&changed);
    assert(s_setup.ai_levels[2] != changed.ai_levels[2]);
    printf("AI_DIFFICULTY_WIRE|PASS|v%d|all eight seats and four levels roundtrip; invalid bytes normalize; changes unready; joiner cannot write\n", NM_VERSION);
    return 0;
}
