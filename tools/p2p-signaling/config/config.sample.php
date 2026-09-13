<?php
/**
 * Copy to config/config.php (or point DUNECITY_P2P_CONFIG at it) and edit.
 *
 * This file must be outside the web root - `public/` is the DocumentRoot, and `config/` is its
 * sibling, so a default installation already is. There is no default for `state_dir`: the
 * service refuses to start rather than guess a directory that might be served or world-readable.
 */
return [
    // Holds invitation codes, grants and session tokens, so it is private: absolute, with a
    // canonical (symlink-free) parent that is not world-writable.
    //
    // Do NOT create this directory yourself, and do not chown anything to www-data - the deploy
    // account cannot, and does not need to. Create only the PARENT:
    //
    //     mkdir -p /home/dunelegacy/private
    //     chgrp www-data /home/dunelegacy/private
    //     chmod 2770     /home/dunelegacy/private
    //
    // The PHP worker creates the child below on its first request, 0700 and owned by itself.
    'state_dir' => '/home/dunelegacy/private/p2p-state',

    // The base url this service is reachable at, with no trailing slash. The admission answer
    // reports `url=<base>/v1/p2p` and `signaling=<base>`.
    'public_base_url' => 'https://dunelegacy.com/p2p',

    // Exactly the browser origins allowed to call this service. Never '*', never 'null'.
    'allowed_origins' => [
        'https://dunelegacy.com',
        'https://www.dunelegacy.com',
    ],

    // STUN only. A turn: url is refused by the configuration loader, because gameplay through a
    // third party is not something this deployment does.
    'ice_servers' => [
        'stun:stun.l.google.com:19302',
        'stun:stun1.l.google.com:19302',
    ],

    // Plaintext HTTP is refused unless this is on AND the caller is on the loopback interface.
    'allow_plaintext_loopback' => false,

    // The path prefix this service is mounted at, if index.php cannot work it out from
    // SCRIPT_NAME. Leave unset for a normal Apache DocumentRoot or Alias.
    // 'base_path' => '/p2p',

    'app' => 'dunecity',
    // 0 disables the check; otherwise only this NETWORK_PROTOCOL_VERSION is admitted.
    'required_game_protocol' => 0,

    'analytics_enabled' => true,
    'log_enabled' => true,
];
