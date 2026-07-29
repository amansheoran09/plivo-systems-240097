# NOTES

Design: the sender emits one data packet per frame and, immediately after it, one XOR
parity packet carrying `frame[i] XOR frame[i-1]`, both in a 3-byte-header wire format
(`[type:1][seq:2][payload:160]`, 163 B); a cumulative byte governor suppresses the parity
whenever total transmitted bytes would exceed 1.95x the raw stream, which costs about 8%
of parities and keeps the run legal on any profile without hand-tuning. This is a rate-1/2
convolutional erasure code, chosen over plain duplication because it costs the same bytes
but gives each frame three recovery paths instead of two, and over ARQ because a NACK plus
its retransmission crosses the hostile relay twice — on profile B that is two draws from a
20–80 ms distribution, which cannot fit inside any delay worth scoring. The receiver
dedupes by sequence, XOR-repairs erasures with a work-queue cascade so that one recovered
frame can unlock a parity it is already holding and chain onward, and forwards each
payload to the player the instant it becomes known. There is deliberately no playout clock
on the receiver: the harness player scores first-arrival-before-deadline and never
penalises early arrival, so the graded playout buffer is the `delay_ms` parameter itself,
and holding frames back could only ever lose. The receiver sends no feedback at all, which
keeps the entire 2.0x budget on the media path.

Grade at `--delay_ms 110`. What breaks it: bursts longer than roughly two frames, because
a rate-1/2 chain repairs at most one erasure per parity and the data packet and its parity
are emitted back-to-back and so die together — on a self-authored profile with 70%
in-burst loss the miss rate plateaus near 2% and no amount of playout delay recovers it.
It also breaks if the unseen profile's jitter tail runs past ~95 ms, since the last
repair path is only 20 ms behind the data and everything after that is deadline, not
mechanism.
