- **Windows: the kernel network ETW source reads each event by its own
  template.** Microsoft-Windows-Kernel-Network lays its TCP events out in
  three templates that share a prefix (PID, size, the endpoints) and differ
  in what sits before the trailing seqnum and connection id: a connect or
  accept carries the TCP options (16 bytes), a send its start and end time
  (8 bytes), a receive or disconnect nothing. The source read every event
  by the last layout, so a connect's connection id was 16 bytes of options
  and a send's was its times, and the bytes summed per connection could
  never meet the disconnect that reports them. The parser is now
  `NetworkMonitorKernelNetwork.h` in the core - pure, one template per
  event id, every length checked - and tested from payloads built per
  template on every platform, like the conntrack parser.
  - Both ETW sources enable the provider with every keyword rather than a
    mask of zero, and the DNS client source names the asking process from
    the Toolhelp process list, as the network source and the socket table
    do, instead of `pid N`.
  - `scripts/netmon-etw-acceptance.ps1` is the first elevated run as a test
    with a verdict: it records with the ETW sources alone, makes a loopback
    connection and a DNS query of its own, reads the recording back and
    checks the opened, accepted and closed events (with bytes) and the DNS
    observation, each attributed to the PowerShell that made them.
    *Windows: the first elevated run* in the module README is the
    procedure and what each failure would mean.
