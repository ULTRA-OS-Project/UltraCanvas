- **CI's Linux row and a cloud session build the MQTT, AMQP, CoAP, SNMP and
  SSH plug-ins.** Neither installed paho-mqtt-c, rabbitmq-c, libcoap3,
  net-snmp or libssh, so those five UltraNet plug-ins were built on no
  Linux at all and a change to them - the entry-point guard of 2026-10, for
  one - was compiled nowhere before it merged. `.github/workflows/build.yml`'s
  Linux rows and `.claude/hooks/session-start.sh` now install the five
  Ubuntu packages, and a step after configure fails the Linux row when one
  of the plug-ins was left out, as the Hunspell check does.
