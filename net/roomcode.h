/* net/roomcode.h -- THE ROOM CODE: a relayed host's address, in six characters.
 *
 * Over a relay there is no IP address to type. Peers address each other by a 32-bit
 * tunnel id, and a joiner needs the HOST'S id before it can send its first packet --
 * the relay will not tell it. Every other id is free: a joiner draws its own at random
 * and the host learns it from the arriving packet, exactly as it learns an IP today.
 * So the host's id IS the room code, and this file is the whole of the invention. No
 * service, no allocation step, no extra field on the wire, and no NM_VERSION bump: the
 * relay lives entirely below the handshake.
 *
 * SIX SYMBOLS, WHICH IS THIRTY BITS AND NOT THIRTY-TWO. That is a constraint on the
 * HOST's id rather than a lossy encoding: a host draws in 1..0x3FFFFFFF so that its id
 * always survives the round trip. A joiner's id is never encoded and uses the full
 * width.
 *
 * CROCKFORD BASE32, because this gets read out loud. The alphabet omits I, L, O and U;
 * decoding folds I and L to 1 and O to 0, so the four ways a person can mishear or
 * mistype a code all land on the right one. U is omitted so that no draw can spell
 * something unfortunate.
 *
 * THE LEADING '#' IS LOAD-BEARING. "K7M3QX" is a syntactically valid DNS label, so
 * without a sigil a room code reaches getaddrinfo through dms_parse_hostport and dies as
 * a failed name lookup -- a confusing error for a correct code. One character outside
 * the hostname charset makes the two grammars disjoint by construction rather than by
 * guesswork, and the host's screen prints the exact string to type, sigil included.
 */
#ifndef CNC3D_ROOMCODE_H
#define CNC3D_ROOMCODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* "#XXX-XXX" and a terminator is nine bytes; ten is the buffer to pass. */
#define RC_TEXT_MAX 10
/* The largest id a six-symbol code can carry, and therefore the largest a HOST may draw. */
#define RC_HOST_ID_MAX 0x3FFFFFFFul

/* id -> "#K7M-3QX". Returns 1 on success, 0 if the id is 0 or above RC_HOST_ID_MAX
   (which would not survive the round trip and must never be silently truncated). */
int rc_encode(unsigned long id, char out[RC_TEXT_MAX]);

/* "#K7M-3QX" -> id. Case-insensitive; hyphens and spaces are ignored anywhere; I and L
   read as 1 and O as 0. The leading '#' is required. Returns 1 on success, 0 for
   anything it will not guess at: a missing sigil, the wrong number of symbols, a
   character outside the alphabet, or a decoded id of zero. */
int rc_decode(const char* text, unsigned long* out_id);

/* A host's id: uniform in 1..RC_HOST_ID_MAX, so it always encodes.
   A peer's id: uniform in 1..0xFFFFFFFE, the full range the protocol allows, because it
   is never written down. Both refuse the two reserved values -- 0 is "addressed to
   nobody", which registration uses, and 0xFFFFFFFF is the relay's own broadcast.
   Rejection sampling, never modulo: a modulo fold would make some ids likelier than
   others, and an id is the only thing standing between a room and a stranger. */
unsigned long rc_draw_host_id(void);
unsigned long rc_draw_peer_id(void);

#ifdef __cplusplus
}
#endif
#endif /* CNC3D_ROOMCODE_H */
