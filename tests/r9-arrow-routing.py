#!/usr/bin/env python3
"""Execute extracted production pre-IME and hardware predicate with fake Android events.
Models the observed framework behavior: DOWN continues to dispatch; UP is consumed.
No device/UI events. Also verifies the baseline fails that exact regression.
"""
from pathlib import Path
import subprocess, tempfile, json
root=Path(__file__).resolve().parents[1]
view=root/'lorie/src/main/java/com/termux/x11/LorieView.java'
sender=root/'lorie/src/main/java/com/termux/x11/input/InputEventSender.java'
def extract(text, signature):
    start=text.index(signature); opening=text.index('{',start); depth=1; i=opening+1
    while depth:
        depth += (text[i]=='{')-(text[i]=='}'); i+=1
    return text[start:i]
preime=extract(view.read_text(),'public boolean dispatchKeyEventPreIme(')
hardware=extract(sender.read_text(),'public static boolean isHardwareKeyboardEvent(')
# Regression fixture extracted verbatim from LorieView at retained 22b880a.
baseline=(root/'tests/r9-arrow-preime-baseline.java.txt').read_text().strip()

template=r"""
import java.util.*;
public class RoutingTest {
 static class InputDevice {
  static final int KEYBOARD_TYPE_ALPHABETIC=2, SOURCE_KEYBOARD=257;
  boolean virtual=false; int type=2, sources=257;
  boolean isVirtual(){return virtual;} int getKeyboardType(){return type;}
  boolean supportsSource(int source){return (sources&source)==source;}
 }
 static class KeyEvent {
  static final int ACTION_DOWN=0,ACTION_UP=1,ACTION_MULTIPLE=2,FLAG_SOFT_KEYBOARD=2;
  static final int KEYCODE_DPAD_UP=19,KEYCODE_DPAD_DOWN=20,KEYCODE_DPAD_LEFT=21,KEYCODE_DPAD_RIGHT=22;
  int key,action,flags=8,source=257,deviceId=9,repeat=0,meta=0;
  InputDevice device=new InputDevice();
  KeyEvent(int k,int a){key=k;action=a;}
  int getKeyCode(){return key;} int getAction(){return action;}
  int getFlags(){return flags;} int getDeviceId(){return deviceId;}
  InputDevice getDevice(){return device;}
  boolean isFromSource(int s){return (source&s)==s;}
 }
 static class InputEventSender { HARDWARE }
 static class ArrowKeyTrace {static void event(String s,KeyEvent e){}}
 static class MainActivity {
  static final MainActivity instance=new MainActivity();
  static MainActivity getInstance(){return instance;}
  ArrayList<KeyEvent> seen=new ArrayList<>(); boolean accepted=true;
  boolean handleKey(KeyEvent e){seen.add(e);return accepted;}
 }
 static class ReleaseHandler {void sendEmptyMessageDelayed(int key,int delay){}}
 final Set<Integer> imeBuggyKeys=Set.of(67,113,114,59,60);
 ReleaseHandler keyReleaseHandler=new ReleaseHandler();
 boolean hardwareKbdScancodesWorkaround=true;
 PREIME
 void deliver(KeyEvent e,boolean ordinaryDispatchAvailable){
  if(!dispatchKeyEventPreIme(e)&&ordinaryDispatchAvailable) MainActivity.getInstance().handleKey(e);
 }
 static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
 static void reset(){MainActivity.instance.seen.clear();MainActivity.instance.accepted=true;}
 public static void main(String[] ignored){
  RoutingTest v=new RoutingTest();
  for(int key=19;key<=22;key++){
   reset();KeyEvent down=new KeyEvent(key,0),up=new KeyEvent(key,1);
   v.deliver(down,true);v.deliver(up,false);
   check(MainActivity.instance.seen.size()==2,"REGRESSION: missing physical arrow release");
   check(MainActivity.instance.seen.get(0)==down&&MainActivity.instance.seen.get(1)==up,"event identity/order");
   reset();v.deliver(down,true);v.deliver(up,true);
   check(MainActivity.instance.seen.size()==2,"duplicate forwarding when framework supports both edges");
   reset();KeyEvent repeat=new KeyEvent(key,0);repeat.repeat=1;repeat.meta=0x1000;
   v.deliver(down,true);v.deliver(repeat,true);v.deliver(up,true);
   check(MainActivity.instance.seen.size()==3&&MainActivity.instance.seen.get(1)==repeat,"repeat/modifier event unchanged");
   reset();MainActivity.instance.accepted=false;
   check(!v.dispatchKeyEventPreIme(down),"preserve handler rejection / disconnected behavior");
   check(MainActivity.instance.seen.size()==1,"invoke original handler once");
  }
  // Unknown, virtual, software, non-keyboard and ACTION_MULTIPLE unchanged.
  for(int variant=0;variant<9;variant++) for(int action=0;action<2;action++){
   reset();KeyEvent e=new KeyEvent(20,action);
   switch(variant){
    case 0:e.deviceId=-1;break;case 1:e.device=null;break;
    case 2:e.device.virtual=true;break;case 3:e.device.type=1;break;
    case 4:e.device.sources=0;break;case 5:e.source=0;break;
    case 6:e.flags|=2;break;case 7:e.action=2;break;case 8:e.source=0x1000010;break;
   }
   check(!v.dispatchKeyEventPreIme(e)&&MainActivity.instance.seen.isEmpty(),"nonhardware scope variant="+variant);
  }
  for(int key=0;key<=300;key++) if(key<19||key>22)for(int action=0;action<2;action++){
   reset();check(!v.dispatchKeyEventPreIme(new KeyEvent(key,action))&&MainActivity.instance.seen.isEmpty(),"nonarrow scope key="+key);
  }
  // Existing workaround-off behavior delegates every event as before.
  v.hardwareKbdScancodesWorkaround=false;
  for(int key=0;key<=300;key++){
   reset();KeyEvent e=new KeyEvent(key,1);e.flags|=2;
   check(v.dispatchKeyEventPreIme(e)&&MainActivity.instance.seen.get(0)==e,"workaround-off unchanged");
  }
  System.out.println("PASS observed swallowed-UP regression, no duplicates, existing scope/handler semantics");
 }
}
"""
with tempfile.TemporaryDirectory(prefix='r9-arrow-routing-') as tmp:
    tmp=Path(tmp)
    results={}
    for label,method in [('baseline',baseline),('candidate',preime)]:
        code=template.replace('HARDWARE',hardware).replace('PREIME',method)
        (tmp/'RoutingTest.java').write_text(code)
        subprocess.run(['javac',str(tmp/'RoutingTest.java')],check=True,capture_output=True,text=True)
        run=subprocess.run(['java','-cp',str(tmp),'RoutingTest'],capture_output=True,text=True,timeout=10)
        if label=='baseline':
            assert run.returncode!=0 and 'REGRESSION: missing physical arrow release' in run.stderr,run.stdout+run.stderr
            results[label]='EXPECTED_FAIL: missing physical arrow release'
        else:
            assert run.returncode==0,run.stdout+run.stderr
            results[label]=run.stdout.strip()
    print(json.dumps({'status':'PASS','results':results,'runtime_device_test':False},indent=2))
