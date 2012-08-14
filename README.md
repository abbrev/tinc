tinc
====

tinc allows you to talk with your calculator over the link port through the
terminal or over the network. One use for this is to use the Punix shell in a
real terminal, rather than the Punix built-in terminal on the calculator (which
for some users has a hand-crampingly small keyboard).

Here is an example session using tinc:

	$ ./tinc
	ticables-INFO: ticables library version 1.3.3
	ticables-INFO: setlocale: en_US.UTF-8
	ticables-INFO: bindtextdomain: /usr/local/share/locale
	ticables-INFO: textdomain: libticables2
	ticables-INFO: kernel: 2.6.43.8-1.fc15.i686.PAE
	ticables-INFO: Link cable handle details:
	ticables-INFO:   model   : TiEmu
	ticables-INFO:   port    : #1
	ticables-INFO:   timeout : 1.5s
	ticables-INFO:   delay   : 10 us


	server login: root
	password:
	stupid shell v0.2
	root@server:~# help
	available applets:
	 tests     top       cat       echo      true      false     clear     uname
	 env       id        pause     batt      date      adjtime   malloc    pid
	 pgrp      poweroff  times     sysctltest ps        bt        crash     mul
	 div       kill      time      exit      status    help
	root@server:~# echo hello world!
	hello world!
	root@server:~# exit

	server login:

Note: this initial version of tinc supports only local communication; i.e., in
the terminal. It can still be networked using nc with pipes or nc's -e option
(though apparently only some versions of nc support this option).
