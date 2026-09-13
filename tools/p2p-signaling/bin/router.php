<?php
declare(strict_types=1);

/**
 * Router for `php -S 127.0.0.1:8080 bin/router.php`, used by the tests and for local development.
 *
 * The built-in server has no .htaccess, so this reproduces the two things the Apache
 * configuration does: mount everything on the front controller, and serve nothing else.
 */
$path = parse_url((string)($_SERVER['REQUEST_URI'] ?? '/'), PHP_URL_PATH);
$_SERVER['SCRIPT_NAME'] = '/index.php';
unset($path);
require __DIR__ . '/../public/index.php';
return true;
