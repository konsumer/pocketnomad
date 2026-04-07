This is a Reticulum/LXMF client for the CardputerADV with CAP LoRa 1262. I may support other hardware later, but this is my initial target.


### features

- communicate with other nomads (lora, tcp, udp)
- password-protected identity: reset, and it's secure from tampering


### nomad

When I refer to "nomad" I mean lxmf + reticulum. There are several desktop clients, and if you use the same settings (frequency/etc) you should be able to talk to them.

- reticulum is encrypted. only the intended recipient can read the message
- reticulum spans several interface protocols, so you can talk over the internet, lora, etc


## development

```sh
# compile and upload
pio run --target upload

# monitor serial
pio device monitor
```