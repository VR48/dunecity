// Local-only real-browser RTC smoke harness. Stop with Ctrl-C.
import { build } from 'esbuild';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';
import { randomBytes } from 'node:crypto';
const port=Number(process.argv[2] ?? 8770);
if(!Number.isInteger(port)||port<1024||port>65535)throw new Error('Invalid local port');
const bundle=await build({entryPoints:[fileURLToPath(new URL('../../platform/web/p2pkit/src/transports/rtc.ts',import.meta.url))],bundle:true,platform:'browser',format:'esm',write:false});
const page=await readFile(new URL('./browser-smoke.html',import.meta.url));
const server=createServer((req,res)=>{
 const pathname=new URL(req.url,'http://127.0.0.1').pathname;
 if(pathname!=='/'&&pathname!=='/rtc.mjs'){res.writeHead(404);res.end();return}
 res.writeHead(200,{'Content-Type':pathname==='/'?'text/html; charset=utf-8':'text/javascript; charset=utf-8','Cache-Control':'no-store'});
 res.end(pathname==='/'?page:bundle.outputFiles[0].contents);
});
server.listen(port,'127.0.0.1',()=>{
 const room=randomBytes(8).toString('hex');
 for(const peer of [1,2])console.log(`http://127.0.0.1:${port}/?peer=${peer}&room=${room}`);
});
