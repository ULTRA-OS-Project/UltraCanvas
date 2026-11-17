- **The ChatCube relay deploy reaches a server whose SSH listens on another
  port.** The `ChatCube relay` workflow always connected on port 22. It now
  reads `CHATCUBE_RELAY_SSH_PORT` - an Actions secret or variable, 22 when
  neither is set - for both of its SSH steps, and stops with an error naming
  it when it is not a number. The README says how to pin that server's key:
  `ssh-keyscan -p <port> <host>`, whose lines name it `[host]:port`.
