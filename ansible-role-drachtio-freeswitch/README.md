# ansible-role-drachtio-freeswitch

This is an ansible role for building a [Freeswitch](https://freeswitch.org/) that is configured to  with support for [grpc](https://github.com/grpc/grpc) compiled in, in order to talk to google cloud services.

## Role variables

Available variables are listed below, along with default values (see defaults/main.yml):

```
freeswitch_version: v1.6 
```
specifies which version of freeswitch you want to install

>  Note: only version v1.6 has been tested.  The provided patchfiles may not work with other releases.

```
freeswitch_sources_path: /usr/local/src/freeswitch/
```
path to the FreeSwitch source directory

```
freeswitch_path: /usr/local/freeswitch/
```
path to the FreeSwitch install directory

```
freeswitch_secret: ClueCon 
```
freeswitch secret

```
freeswitch_sip_port: 5080
freeswitch_dtls_sip_port: 5081
```
sip ports to listen on

```
freeswitch_socket_acl:
  - 127.0.0.1/32
```
add any addresses (in cidr format) that should be allowed to connect to event socket

```
freeswitch_owner: freeswitch
freeswitch_group: daemon
```
freeswitch owner and group

```
freeswitch_modules_template: ../templates/modules.conf 
```
modules.conf file used for FreeSwitch compilation

```
freeswitch_configure_command: configure 
```
freeswitch configure command - you can add option

```
freeswitch_log_conf_template: ../templates/logfile.conf.xml

```
freeswitch log configuration file template

## Example playbook
```
---
- hosts: all
  become: yes
  roles:
    - ansible-role-drachtio
  become: yes
```
