- **The ChatCube relay workflow copies the tested sources to the server and
  installs nothing.** Its deploy job ran `update.sh` over SSH through a sudo
  rule, which gave the deploy user root for that command. It now only puts
  this commit's sources in `~/chatcube-relay-src` of the deploy user (the
  previous copy replaced whole) and says in its summary which version it
  copied; the operator logs in and runs the copy's `deploy/update.sh`
  themselves. The step that asked the public address for the new version
  went with it, and so did `vars.CHATCUBE_RELAY_URL`. The deploy user needs
  no sudo rule any more - the README (Deploying from CI) says what to remove
  on a server set up for the earlier workflow.
