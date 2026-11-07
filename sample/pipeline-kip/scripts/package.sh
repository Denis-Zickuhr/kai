#!/usr/bin/env bash
# VERSION and CHANNEL come from the step's params (the wizard's answers).
mkdir -p .state
echo "packaging $VERSION ($CHANNEL)..."; sleep 1
echo "$VERSION-$CHANNEL" > .state/package
echo "wrote kai-$VERSION-$CHANNEL.zip"
