using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using HidLibrary;
using System.Text.RegularExpressions;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;


namespace HidFormTest
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();
        }

        [DllImport("user32.dll", SetLastError = true)]
        static internal extern IntPtr SendMessage(IntPtr hWnd, int Msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", EntryPoint = "WindowFromPoint", CharSet = CharSet.Auto, ExactSpelling = true)]
        static internal extern IntPtr WindowFromPoint(Point point);

        [DllImport("user32.dll", SetLastError = true)]
        static internal extern void SetCursorPos(int x, int y);

        [DllImport("user32.dll", SetLastError = true)]
        static internal extern void mouse_event(int dwFlags, int dx, int dy, int cButtons, int dwExtraInfo);

        [DllImport("hid.dll", SetLastError = true)]
        static internal extern bool HidD_SetOutputReport(IntPtr hidDeviceObject, byte[] lpReportBuffer, int reportBufferLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        static internal extern bool ReadFile(IntPtr hFile, [Out] byte[] lpBuffer, uint nNumberOfBytesToRead, out uint lpNumberOfBytesRead, IntPtr lpOverlapped);

        public const int BM_CLICK = 0x00F5;
        public const int MOUSEEVENTF_LEFTDOWN   = 0x02;
        public const int MOUSEEVENTF_LEFTUP     = 0x04;
        public const int MOUSEEVENTF_RIGHTDOWN  = 0x08;
        public const int MOUSEEVENTF_RIGHTUP    = 0x10;

        private UInt16 lastId = 0;
        private Graphics g;
        public delegate void ReadHandlerDelegate(HidReport report);
        public delegate void ReadTaskDelegate(HidDevice device);
        private HidDevice ftouch = null;
        private Thread readtask;

        public static bool FastWrite(HidDevice device, byte[] outputBuffer)
        {
            try
            {
                if (HidD_SetOutputReport(device.Handle, outputBuffer, outputBuffer.Length))
                    return true;
                else
                    return false;
            }
            catch
            {
                return false;
            }
        }

        public static int FastRead(HidDevice device, byte[] inputBuffer)
        {
            try
            {
                uint bytesRead;
                if (ReadFile(device.Handle, inputBuffer, (uint)inputBuffer.Length, out bytesRead, IntPtr.Zero))
                {
                    return (int)bytesRead;
                }
                else
                {
                    return 0;
                }
            }
            catch (Exception)
            {
                return -1;
            }
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            this.FormBorderStyle = FormBorderStyle.None;
            //this.WindowState = FormWindowState.Maximized;
            Debug.WriteLine("Device not connected!");
            //panel1.Width = Screen.PrimaryScreen.Bounds.Width;
            //panel1.Height = Screen.PrimaryScreen.Bounds.Height;
            TopMost = true;
            g = panel1.CreateGraphics();
            CheckDevice();
            readtask = new Thread(new ThreadStart(ReadTask));
        }

        private void CheckDevice()
        {
            g.Clear(Color.Blue);

            Thread thread = new Thread(new ThreadStart(ConnectFtouch));
            thread.Start();
        }

        private void ConnectFtouch()
        {
            while (ftouch == null || !ftouch.IsConnected)
            {
                Debug.WriteLine("Connecting to device...");
                HidDevice[] dev = HidDevices.Enumerate().ToArray();

                var i = 0;
                while (i < dev.Count())
                {
                    if (Regex.IsMatch(dev[i].DevicePath, @".*1915.*eeee.*col03", RegexOptions.Singleline | RegexOptions.IgnoreCase))
                    {
                        ftouch = dev[i];
                        ftouch.OpenDevice();
                        readtask.Start();
                        break;
                    }
                    i++;
                }
            }
            Debug.WriteLine("Device connected!");
        }

        private void ReadTask()
        {
            int ret;
            int touchCount = 0;
            int lastTouchCount = 0;
            byte[] buf = new byte[9];
            int readCount = 0;
            UInt16 tsLog = 0;
            UInt16 tsStatChg = 0;
            int curTouchState = 0;
            int newTouchState = 0;


            while (true)
            {
                ret = FastRead(ftouch, buf);

                if (ret > 0)
                {
                    UInt16 ts = BitConverter.ToUInt16(buf, 1);
                    UInt16 x = BitConverter.ToUInt16(buf, 3);
                    UInt16 y = BitConverter.ToUInt16(buf, 5);
                    UInt16 z = BitConverter.ToUInt16(buf, 7);
                    int xpos = (int)(x / 65535.0 * (panel1.Width - 30));
                    int ypos = (int)((1 - y / 65535.0) * (panel1.Height - 30));

                    UInt16 tsDiff = (UInt16)(ts - tsLog < 0 ? ts - tsLog + 65536 : ts - tsLog);

                    if (tsDiff > 0)
                    {
                        g.Clear(Color.Black);
                        //Debug.WriteLine("Touch count: " + touchCount.ToString());

                        if (touchCount != curTouchState)
                        {
                            if (touchCount != newTouchState)
                            {
                                newTouchState = touchCount;
                                tsStatChg = tsLog;
                            }
                            else if (ts - tsStatChg > 200)
                            {
                                curTouchState = newTouchState;
                                Debug.WriteLine("State Changed: " + curTouchState.ToString());
                            }
                        }
                        else
                        {
                            newTouchState = curTouchState;
                        }

                        touchCount = 0;

                        if (tsDiff > 980)
                        {
                            Debug.WriteLine("Idle Mode... " + ts.ToString());
                        }
                    }

                    if (z > 0x00 && z != 0xFFFF)
                    {
                        g.DrawEllipse(new Pen(Color.Gray, 5), new Rectangle(xpos, ypos, 30, 30));
                        g.DrawEllipse(new Pen(Color.Green, 3), new Rectangle(xpos, ypos, 30, 30));
                        g.DrawEllipse(new Pen(Color.LightGreen, 1), new Rectangle(xpos, ypos, 30, 30));
                        touchCount++;
                    }

                    tsLog = ts;
                }

            }
            //CheckDevice();
        }

        private void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            readtask.Abort();
        }
    }
}
