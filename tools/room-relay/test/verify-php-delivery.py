"""Exercise the actual relay publisher and PHP receiver with disposable SQLite data.

This checks their signed event contract for both server-observed transports, not an actual
WSS or HTTPS-polling multiplayer match.
"""
import argparse, os, pathlib, shutil, socket, sqlite3, subprocess, tempfile, time
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--metaserver-dir', type=pathlib.Path, required=True)
args = parser.parse_args()
if not (args.metaserver_dir / 'relay-events.php').is_file():
    parser.error('--metaserver-dir must contain relay-events.php')
relay_module = pathlib.Path(__file__).resolve().parents[1] / 'src/analytics.js'
node_binary, php_binary = shutil.which('node'), shutil.which('php')
if not node_binary or not php_binary:
    parser.error('node and php CLI are required')
with tempfile.TemporaryDirectory(prefix='dune-relay-php-') as tmp:
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
    key='local-contract-test-key-'*3
    env={**os.environ,'DATA_DIR':tmp,'DUNE_RELAY_ANALYTICS_KEY':key}
    with open(pathlib.Path(tmp)/'php.log','w') as log:
        php=subprocess.Popen([php_binary,'-S',f'127.0.0.1:{port}','-t',str(args.metaserver_dir.resolve())],env=env,stdout=log,stderr=log)
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(('127.0.0.1',port),.1): break
                except OSError: time.sleep(.05)
            js=r'''
const {LifecyclePublisher}=require(process.env.RELAY_MODULE);
// One room per server-observed transport. The relay labels the room from its own ingress
// configuration; nothing in a participant hook can change that label.
const ROOMS=[['wss','contract-room-wss-aaaaaa'],['https-poll','contract-room-poll-bbbbb']];
(async()=>{
  let delivered=0;
  for (const [transport,roomLogId] of ROOMS) {
    const pub=new LifecyclePublisher({destination:new URL(process.env.TEST_ENDPOINT),key:process.env.DUNE_RELAY_ANALYTICS_KEY,transport,timeoutMs:1000,backoffMs:1});
    pub.roomCreated({roomLogId});
    for (const [participantId,runtime] of [[1,'browser'],[2,'native']]) pub.participantJoined({roomLogId,participantId,runtime,appVersion:'1.0.655'});
    pub.matchStarted({roomLogId});
    for (const [participantId,runtime] of [[2,'native'],[1,'browser']]) pub.participantLeft({roomLogId,participantId,runtime,appVersion:'1.0.655',reason:'left'});
    pub.roomClosed({roomLogId,reason:'host_left'});
    await pub.stop();
    delivered+=pub.stats.delivered;
    console.log(JSON.stringify({transport,...pub.stats}));
  }
  if(delivered!==14) process.exitCode=1;
})();
'''
            subprocess.run([node_binary,'-e',js],env={**env,'RELAY_MODULE':str(relay_module),'TEST_ENDPOINT':f'http://127.0.0.1:{port}/relay-events.php'},check=True)
            con=sqlite3.connect(str(pathlib.Path(tmp)/'games.sqlite'))
            rows=con.execute('select kind, client_runtime, transport, source from analytics_relay_events order by rowid').fetchall()
            assert len(rows)==14,rows
            for transport in ('wss','https-poll'):
                served=[r for r in rows if r[2]==transport]
                assert len(served)==7,served
                assert [r[0] for r in served]==['created','joined','joined','started','left','left','closed'],served
                assert [r[1] for r in served if r[0]=='joined']==['browser','native'],served
            assert all(r[3]=='relay_service_v1' for r in rows),rows
            # The reporting view keeps the two apart, and keeps saying which side observed what.
            assert con.execute("""select transport, client_runtime, transport_source, runtime_source
                from analytics_relay_participants order by transport, participant_id""").fetchall()==[
                ('https-poll','browser','server_observed','client_reported'),
                ('https-poll','native','server_observed','client_reported'),
                ('wss','browser','server_observed','client_reported'),
                ('wss','native','server_observed','client_reported')]
            print('PASS: real Node publisher -> PHP HMAC validation -> additive SQLite lifecycle records;')
            print('      wss and https-poll recorded as distinct transports; browser/native markers retained.')
            print('Contract fixture only: this does not attest an actual WSS or HTTPS-polling match.')
        finally:
            php.terminate(); php.wait(timeout=5)
