# M5 PaperColor Calendar  

<img src="Properties/pcc01.png" align="center" Width="480" alt="M5PaperColor Calendar" />  

### 1. 概要  
見ての通り`M5 PaperColor`の卓上万年カレンダーです。 カラー電子ペーパーモデルでみんなが最初に思いつきそうな使い道を実現してみたくて作りました。それだけです。  
[`M5Burner`](https://docs.m5stack.com/en/uiflow/m5burner/intro)から簡単に使えるようにするため、ファームウェア以外の外部ファイルは使用しません。  
設定もすべてWiFiの使えるデバイス(スマホ等)から行います。  
カレンダーですので基本的には1日5分しか動作しないため、バッテリーによる長期動作を期待しています。

### 2. 必要環境  
M5 PaperColor(以下「本体」と呼称)以外に必要なものは
+ ファームウエア転送用のマシン  
+ WiFiの使えるスマホ等のデバイス(設定入力用)  

以外ありません。設定は内部フラッシュ(NVS)に保存しますのでSDカードも使用しません。  

### 3. 導入方法  
[`M5Burner`](https://docs.m5stack.com/en/uiflow/m5burner/intro)で`Download`->`Burn`するのが簡単です。  
`esptool.py`や`M5Launcher`をどうしても使いたい人(いるのか?)用にバイナリファイルもこのページ右側の[`Releases`](../../releases)の中にzip形式で置いています。  

### 4. 初回の起動(WiFiの設定)  
<img src="Properties/init.png" width="320" align="right" />

**4-1.** 初回起動時、WiFi設定を促すシンプルな画面が本体に表示されます。画面に従い、WiFiの使えるデバイスからSSID `M5 PaperColor Init mode`のアクセスポイントを選択し、本体とWiFiで接続してください。パスワードは`00000000`(ゼロ8つ)固定です。(**これでお分かりとは思いますが、ここのセキュリティのレベルは低いので注意して使用ください**)  
<br clear="right" />  
<img src="Properties/init_mode.png" height="480" align="right" />
**4-2.** 接続されると無料WiFiなどでよく見るような感じ(キャプティブWiFi)でWiFiの設定画面になります。`Configure WiFi`を選んで、お使いのWiFiの`SSID`と`Password`を入力してください。`Static IP`のネットワークを使用する場合はその下の欄も入力してください。(`DHCP`の場合はブランクのままにしてください)  
**4-3.** `Save`のボタンを押してしばらく待つと、本体に設定が保存されます。その後本体が自動で再起動します。  
**4-3.** 電子ペーパー特有の長い初期化期間の後、ネットワーク時間(`NTP`)を取得して自動的に今月のカレンダーを表示します。 
<br clear="right" />  

### 5. 使用方法
+ ケーブルをつないでいない状態で無操作状態が5分続くと、スリープモード(要は電源OFF)になりバッテリー消費を減らします(設定で禁止していない場合)。電子ペーパーの本領発揮を味わいましょう。  
+ 指定時間(デフォルトの0ならAM0時5分)になると自分で再起動(≠レジューム)し、`NTP`の取得とカレンダー表示の更新を行います。その後は5分無操作によりスリープに入るのは同じです。
+ スリープ中(普通なら1日のうち23時間55分の間)、操作をしたくなった場合は左上のボタンを押すと再起動(≠レジューム)します。緑のLED🟢が点滅したら操作が可能になります。その後は5分無操作によりスリープに入るのは同じです。  
+ 右上のLEDが緑🟢(もしくは赤)に点滅している間は以下の操作が可能です。  
  
  - ボタンC: `NTP`を再取得し、表示を更新します。  
  - ボタンA: `NTP`を取得せずに、今表示している月の翌月を表示します。  
  - ボタンB: `NTP`を取得せずに、今表示している月の前月を表示します。  
  - 上左ボタン: 電源ボタンですので、機能はH/W仕様どおり(なはず)です。  

  **特殊操作**は以下
  
  - ボタンA **3秒長押し**: 設定モード(後述)に入ります。(右下青LED🔵が点滅します)  
  - ボタンB **3秒長押し**: **[注意]** WiFi設定の**全てを消去**し、その後再起動します。  
<br /><br />
  
  
<img src="https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1239/arduino_papercolor_button_demo_01.jpg" width="320" align="center" style="transform: rotate(90deg);" alt="Button layout" />  
  
<p align="center"><small>(from <a href="https://docs.m5stack.com/en/arduino/papercolor/button">m5docs</a>)</small><p />  
<br />  
  
### 6. 設定モード(Config mode)について  
<img src="Properties/config_mode.png" height="480" align="right"/>  

**6-1.** 初回起動時と同様の方法でスマホ等からSSID`M5 PaperColor Config mode`のアクセスポイントを選択して下さい。Passwordは`Init mode`と同じです。  
**6-2.** `Setup`を選択すると設定画面が表示されます。  
**6-3.** 必要に応じて各項目を設定ください。`Enable local calendar`をチェックするとそれ以下の項目も設定されます。  
**6-4.** `Save`を押すと保存されます(ちょっと時間がかかります)。`Saved`と表示されればOKです。右上の×ボタンで閉じてください。  
**6-5.** **重要** 設定終了後、**必ず再度ボタンAを3秒以上長押しして**本体も設定モードを抜けてください(使用ライブラリによる制約)。その後、本体が当月のカレンダーを再描画します。本体が設定モードから抜けないとケーブルが刺さっていなくてもスリープモードに入りませんのでバッテリーを消費します。  
<br clear="right" />  
+ **Holidaysについて**  
  + フォーマットはYAML(インデントは2 or 4スペース)です。Keyのみを使っており、値(Value、`:`の後ろ)は使っていません。  
  + `years`はその年ごとに日付が変わるもの(春分・秋分など)、`year-common`は各年共通です。もちろん自分の個人的な休日を追加しても構いません。  
  + ハッピーマンデーのような`3rd-mon`といった指定も可能です。  
  + `#`以降はコメントですので入力は必須ではありません。
  + `Holidays`に保存できる最大サイズは3KBぐらいです。実際はPCなどでYAMLデータを作成してからスマホなどに送り、コピペするのが現実的でしょう。  

### 7. 注意事項  
+ 本体稼働中(非スリープ中)は右上のLEDが緑🟢に短く5秒サイクルで点滅します。またNTP取得中など通信中や設定変更中は右下のLEDが1秒サイクルで青🔵に点滅します。  
+ 本体稼働中(非スリープ中)にバッテリーが概算20%以下になると、右上LEDが短い赤🔴点滅になります。ケーブルを刺して充電してください。概算25%程度まで充電されると緑🟢点滅に戻ります。0から満充電までは3時間ほどのはずです。
+ スリープモードから抜けるときは、レジュームではなく必ず再起動になります。これは本体ハードウエアと`M5Unified`ライブラリの仕様です。  
+ バッテリーの残量表示は概算ですので必ずしも正確ではありません。スリープ中にバッテリー残量が20%以下になってもLEDは点滅しません(なぜって電源がOFFだから)。同様に画面右下にバッテリー警告アイコンが出るのも**画面を描画したときのバッテリー状態を元に表示**していますので、**充電しても再度描画しない限り更新されません**。これも本体ハードウエアと`M5Unified`ライブラリの仕様です。 
+ なにしろ、現時点では本体の発売から間もないせいか、ライブラリ対応などいろいろおかしい点が多いです。特に電源ボタン含め電源周りは不明な点が多く残っていますので、関連する操作などはいろいろ試してみてください。  
+ 表示されるMoon phase・月齢も描画時の概算です。これ以上正確にしようとすると、緻密な軌道計算等が必要になるため、そこまで実装していません。  
+ お分かりの事とは思いますが、電子ペーパーですので再起動からプログラム始動(ドミソのトーン)まではかなり長いです。焦らず待ちましょう。  
+ 曜日や月の名称は変更できません。できるようにする予定もありません。この機能が必要な方はソースコードを直接ご自分で修正してビルドしなおしてください。  


### 8. ライセンス  
This software is released under the [MIT License](LICENSE.txt).  
Library components follow their respective licenses.  

### 9. 開発環境・使用ライブラリ  
VSCode + PlatformIO  
+ m5stack/M5Unified  
+ tzapu/WiFiManager  
+ tobozo/YAMLDuino  
+ adafruit/Adafruit NeoPixel  

### 10. 免責事項  
AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  

### 11. 作成者  
[osamusg](https://github.com/osamusg)  
