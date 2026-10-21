- **A cloud session builds the MQTT, AMQP, CoAP, SNMP and SSH plug-ins.**
  The Claude Code cloud image lacked paho-mqtt-c, rabbitmq-c, libcoap3,
  net-snmp and libssh, and CI's Linux row does not install them either, so
  those five UltraNet plug-ins were never built in a cloud session and a
  change to them - the entry-point guard of 2026-10, for one - reached CI
  untried. `.claude/hooks/session-start.sh` now installs their Ubuntu 24.04
  packages with the rest, and `cmake` enables the five plug-ins.
