#!/usr/bin/env python3

"""
This script is called by platformio on build (see `platformio.ini`)

It reads `config.toml`, where values for different boards are specified, 
tries to read currently connected board MAC address calling `esptool.py`
and returns formatted build_flags to stdout.

To run it needs:
- platformio with esptool (~/.platformio)
  to run it manually, activate platformio's venv, by running:
    . ~/.platformio/penv/bin/activate
- toml
  you can install it by running:
    pip install toml
"""

from collections import defaultdict
from pprint import pprint
import os
import sys
import copy
import subprocess

PLATFORMIO_HOME = os.path.expanduser('~/.platformio')
ESPTOOL_BIN = os.path.join(
    PLATFORMIO_HOME, 'packages/tool-esptoolpy/esptool.py')
PIO_BIN = os.path.join(PLATFORMIO_HOME, 'penv/bin/pio')

try:
    import toml
except ImportError:
    print('Can not import toml. You may want to run `pip install toml`', file=sys.stderr)
    sys.exit(1)

def deepmerge(source, destination):
    for key, value in source.items():
        if isinstance(value, dict):
            # get node or create one
            node = destination.setdefault(key, {})
            deepmerge(value, node)
        else:
            destination[key] = value

    return destination


class Config():
    config_by_mac = {}
    _wrap_in_double_underscore = True

    def __init__(self, only_dump=False) -> None:
        _config = self._read_config()
        _defaults = _config.pop('_defaults', {})
        _null_if_empty = _config.pop('_null_if_empty', [])
        self._wrap_in_double_underscore = _config.pop('_wrap_in_double_underscore', True)
        self._not_a_string = _config.pop('_not_a_string', [])
        for mac, data in _config.items():
            if mac.startswith('_'):
                # something private
                continue
            self.config_by_mac[mac.upper()] = self._build_config(mac,
                                                                 copy.deepcopy(
                                                                     data),
                                                                 copy.deepcopy(
                                                                     _defaults),
                                                                 _null_if_empty)

    def _read_config(self):
        return toml.load('config.toml')

    def _build_config(self, mac, partial, defaults, null_if_empty):
        cfg = deepmerge(partial, defaults)
        for x in cfg.keys():
            if cfg[x]:
                if isinstance(cfg[x], str) and '{' in cfg[x] and '}' in cfg[x]:
                    cfg[x] = cfg[x].format(**cfg)
                continue
            if x in null_if_empty:
                cfg[x] = None
            else:
                raise Exception(
                    "Invalid configuration. Key {} is empty for {}".format(x, mac))
        return cfg

    def get_for_mac(self, mac):
        mac = mac.upper()
        if mac not in self.config_by_mac:
            raise Exception('Configuration not found for mac: {}'.format(mac))
        return self.config_by_mac[mac]
    
    def make_build_flags(self, conf):
        flags = []
        for k, v in conf.items():
            name = '__{}__'.format(k) if self._wrap_in_double_underscore else k
            value = v
            if isinstance(v, str) and k not in self._not_a_string:
                value = '"{}"'.format(v)
            if v is None:
                value = 'NULL';
            flags.append("-D{}='{}'".format(name, value))
        return '\n'.join(flags)


class EspTool():
    def __init__(self) -> None:
        if not os.path.exists(PLATFORMIO_HOME):
            raise Exception(
                "PLATFORMIO_HOME ({}) not found".format(PLATFORMIO_HOME))
        if not os.path.exists(ESPTOOL_BIN):
            raise Exception(
                "ESPTOOL_BIN ({}) not found".format(ESPTOOL_BIN))
        if not os.path.exists(PIO_BIN):
            raise Exception(
                "PIO_BIN ({}) not found".format(PIO_BIN))

    def read_mac(self):
        # process = subprocess.Popen(['echo', 'More output'],
        #                     stdout=subprocess.PIPE,
        #                     stderr=subprocess.PIPE)
        # stdout, stderr = process.communicate()
        # stdout, stderr

        #print('Running `esptool.py read_mac`...')
        stream = os.popen('{} read_mac'.format(ESPTOOL_BIN))
        output = stream.read().strip()

        mac_address = None
        for x in output.splitlines():
            # print('   ->> {}'.format(x))
            if x.startswith('MAC:'):
                mac_address = x.replace('MAC:', '').strip().upper()

        if mac_address:
            # print('Identified connected board as {}'.format(mac_address))
            return mac_address
        raise Exception(
            "Can not identify mac address, please check output of `esptool.py read_mac`")

def main():

    if len(sys.argv)>1 and sys.argv[1] == '-dump':
        # TODO:
        pprint(toml.load('config.toml'))
        return
    c = Config()
    e = EspTool()
    print(c.make_build_flags(c.get_for_mac(e.read_mac())))

    # pprint(c.config_by_mac)


if __name__ == '__main__':
    main()
