# Automation

Everything the web interface does goes through one endpoint, `POST /cli`,
with the console's lines as the body (doc/api.md). A script logs in once and
then posts lines.

## Log in

```bash
curl -c cookies.txt http://${SWITCH_IP}/login -d pwd=${PASSWORD} -i
```

This saves the session cookie in cookies.txt (`session`, HttpOnly).

## Read and change the config

```bash
curl -b cookies.txt http://${SWITCH_IP}/cli --data-binary 'get state.port.*.link'
curl -b cookies.txt http://${SWITCH_IP}/cli --data-binary $'set port.3.name uplink\napply\nsave'
curl -b cookies.txt http://${SWITCH_IP}/config > saved.txt      # the saved config, as get -s
```

Each line's output ends with `ok` or `err <path>: <reason>`; the HTTP status is
200 either way. `tools/rtlpg_client.py --host H cli LINE...` does the same from
Python.

## Firmware update

`tools/fwupdate.py` checks the image, uploads it, waits for the reboot, and,
from firmware before the object model, carries the config over (it reads,
converts and sets it again: `--dry-run` shows the plan first). By hand:

```bash
curl -b cookies.txt http://${SWITCH_IP}/upload -F "uploadedfile=@${FIRMWARE_FILE_PATH}" -i
```

On a compact board the file is `rtlplayground-install-*.bin`. The switch
answers once the image is checked, then resets; wait for it to answer again.

## Port status

```bash
curl -b cookies.txt http://${SWITCH_IP}/cli --data-binary 'get state.port'
```

`state.port.N.link`, `speed`, `duplex`, the counters (`tx_good`, `rx_good`,
`tx_bad`, `rx_bad`) and the learned MACs, per port; `state.port.N.mib` for
the full MIB counters. `/status.json` is gone.
