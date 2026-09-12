<?php
/** Fixed-route HTTPS gateway. No arbitrary upstream, credentials or client IP forwarding. */
declare(strict_types=1);
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
header('Content-Type: application/octet-stream');
function refuse(int $status): never { http_response_code($status); exit; }
if (($_SERVER['HTTPS'] ?? '') !== 'on') refuse(403);
$method = $_SERVER['REQUEST_METHOD'] ?? '';
$uri = $_SERVER['REQUEST_URI'] ?? '';
// No query strings, escaped separators, PATH_INFO tricks or alternate destinations.
$routes = ['/relay/health' => '/v1/health',
    '/relay/v1/admission/host' => '/v1/admission/host',
    '/relay/v1/admission/join' => '/v1/admission/join',
    '/relay/v1/admission/list' => '/v1/admission/list',
    '/relay/v1/admission/visibility' => '/v1/admission/visibility',
    '/relay/v1/lobby/enter' => '/v1/lobby/enter',
    '/relay/v1/lobby/poll' => '/v1/lobby/poll',
    '/relay/v1/lobby/say' => '/v1/lobby/say',
    '/relay/v1/poll/open' => '/v1/poll/open',
    '/relay/v1/poll/exchange' => '/v1/poll/exchange',
    '/relay/v1/poll/close' => '/v1/poll/close'];
if (!isset($routes[$uri])) refuse(404);
$origin = $_SERVER['HTTP_ORIGIN'] ?? null;
$origins = ['https://dunelegacy.com', 'https://www.dunelegacy.com'];
header('Vary: Origin');
if ($origin !== null) {
    if (!in_array($origin, $origins, true)) refuse(403);
    header('Access-Control-Allow-Origin: ' . $origin);
}
if ($method === 'OPTIONS') {
    header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
    header('Access-Control-Allow-Headers: Content-Type, X-Dune-Session');
    header('Access-Control-Max-Age: 600');
    http_response_code(204); exit;
}
if ($method !== ($uri === '/relay/health' ? 'GET' : 'POST')) refuse(405);
$exchange = $uri === '/relay/v1/poll/exchange';
$poll = str_starts_with($uri, '/relay/v1/poll/');
$maxBody = $exchange ? 1048576 : ($poll ? 0 : 16384);
$length = $_SERVER['CONTENT_LENGTH'] ?? '0';
if (!preg_match('/\A[0-9]{1,7}\z/D', $length) || (int)$length > $maxBody) refuse(413);
if (isset($_SERVER['HTTP_TRANSFER_ENCODING'])) refuse(400);
$type = $_SERVER['CONTENT_TYPE'] ?? '';
if ($method === 'POST' && ($poll ? $type !== 'application/octet-stream'
    : !preg_match('/\Aapplication\/x-www-form-urlencoded(?:;\s*charset=UTF-8)?\z/Di', $type))) refuse(415);
$address = $_SERVER['REMOTE_ADDR'] ?? '';
if (!filter_var($address, FILTER_VALIDATE_IP)) refuse(400);
// This directory is outside the web root. Deployment precreates bounded lock files and key.
$private = '/var/www/data/dunecity-relay';
$key = @file_get_contents($private . '/gateway.key');
if ($key === false || !preg_match('/\A[0-9a-f]{64}\n?\z/D', $key)) refuse(503);
$body = file_get_contents('php://input', false, null, 0, $maxBody + 1);
if ($body === false || strlen($body) !== (int)$length || strlen($body) > $maxBody) refuse(400);
$lock = false;
// Keep four slots available for admission, chat and close even when every exchange is waiting.
for ($i = $exchange ? 0 : 12; $i < ($exchange ? 12 : 16); ++$i) {
    $candidate = @fopen($private . '/slots/' . $i, 'r+');
    if ($candidate !== false && flock($candidate, LOCK_EX | LOCK_NB)) { $lock = $candidate; break; }
    if ($candidate !== false) fclose($candidate);
}
if ($lock === false) refuse(503);
try {
    $headers = ['Content-Type: ' . ($poll ? 'application/octet-stream' : 'application/x-www-form-urlencoded'),
        'X-Dune-Gateway: ' . trim($key), 'X-Forwarded-For: ' . $address];
    if ($origin !== null) $headers[] = 'Origin: ' . $origin;
    if (isset($_SERVER['HTTP_X_DUNE_SESSION'])) {
        $session = $_SERVER['HTTP_X_DUNE_SESSION'];
        if (!$poll || !preg_match('/\A[0-9a-f]{64}\z/D', $session)) refuse(400);
        $headers[] = 'X-Dune-Session: ' . $session;
    }
    $curl = curl_init('http://127.0.0.1:18787' . $routes[$uri]);
    $response = '';
    $responseType = 'application/octet-stream';
    curl_setopt_array($curl, [CURLOPT_CUSTOMREQUEST => $method, CURLOPT_HTTPHEADER => $headers,
        CURLOPT_POSTFIELDS => $method === 'POST' ? $body : null,
        CURLOPT_CONNECTTIMEOUT_MS => 500, CURLOPT_TIMEOUT_MS => 2000,
        CURLOPT_FOLLOWLOCATION => false, CURLOPT_MAXREDIRS => 0, CURLOPT_PROXY => '',
        CURLOPT_PROTOCOLS_STR => 'http', CURLOPT_RETURNTRANSFER => false,
        CURLOPT_WRITEFUNCTION => static function ($ch, string $chunk) use (&$response): int {
            if (strlen($response) + strlen($chunk) > 1048576) return 0;
            $response .= $chunk; return strlen($chunk);
        },
        CURLOPT_HEADERFUNCTION => static function ($ch, string $line) use (&$responseType): int {
            if (preg_match('/\Acontent-type:\s*(application\/octet-stream|application\/json|text\/plain)(?:;[^\r\n]*)?\r\n\z/Di', $line, $m)) $responseType = strtolower($m[1]);
            return strlen($line);
        }]);
    $ok = curl_exec($curl);
    $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
    unset($curl);
    if ($ok === false || $status < 200 || $status > 599 || ($status >= 300 && $status < 400)) refuse(502);
    http_response_code($status);
    header('Content-Type: ' . $responseType);
    header('Content-Length: ' . strlen($response));
    echo $response;
} finally {
    flock($lock, LOCK_UN); fclose($lock);
}
