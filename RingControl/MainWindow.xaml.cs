namespace Ring
{
    using System.Linq;
    using System.Collections.Generic;
    using System.ComponentModel;
    using System.Windows;
    using System.Windows.Media;
    using System.Threading.Tasks;
    using System;
    using Device.Net;
    using Hid.Net.Windows;
    using Microsoft.Extensions.Logging;
    using System.Reactive.Linq;


    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private Color _color = default(Color);

        public Color Color
        {
            get
            {
                return _color;
            }

            set
            {
                _color = value;
                SetPixels(value.R, value.G, value.B, value.A);
            }
        }

        public byte A
        {
            get
            {
                return 255;
            }

            set { }
        }

        private IDevice device;

        public MainWindow()
        {
            new Action(async () => await InitializeAsync())();

            InitializeComponent();
            this.DataContext = this;
        }

        public async Task InitializeAsync()
        {
            var loggerFactory = LoggerFactory.Create((builder) =>
            {
                _ = builder.SetMinimumLevel(LogLevel.Trace);
            });

            //Register the factory for creating Hid devices. 
            var hidFactory =
                new FilterDeviceDefinition(vendorId: 0x239A, productId: 32971).CreateWindowsHidDeviceFactory(loggerFactory);
            //Get connected device definitions
            var deviceDefinitions = (await hidFactory.GetConnectedDeviceDefinitionsAsync().ConfigureAwait(false)).ToList().FindAll(d=>d.ProductName== "CamRing");

            if (deviceDefinitions.Count == 0)
            {
                //No devices were found
                return;
            }

            //Get the device from its definition
            device = await hidFactory.GetDeviceAsync(deviceDefinitions.First()).ConfigureAwait(false);

            //Initialize the device
            await device.InitializeAsync().ConfigureAwait(false);
        }

        private void ClearPixels(){
            SetPixels(0,0,0,0);
        }

        private void SetPixels(byte r, byte g, byte b, byte a){
            //Create the request buffer
            var buffer = new byte[65];
            buffer[0] = 0x00; // Report id
            buffer[1] = 0x01; // Instruction - Control
            buffer[2] = 0x01; // Command - ALL Leds
            buffer[3] = 0x00; // Pattern N/A
            buffer[4] = r; // All.R
            buffer[5] = g; // All.Gs
            buffer[6] = b; // All.B
            buffer[7] = a; // All.brightnessss

            try
            {
                //Write and read the data to the device
                new Action(async () => await device.WriteAsync(buffer).ConfigureAwait(false))();

            }
            catch (Exception e)
            {
                Console.Error.WriteLine(e);
            }
        }

        private void Reset(){
            //Create the request buffer
            var buffer = new byte[65];
            buffer[0] = 0x00; // Report id
            buffer[1] = 0x01; // Instruction - Control
            buffer[2] = 0xFF; // Command - Disable OVerride

            try
            {
                //Write and read the data to the device
                new Action(async () => await device.WriteAsync(buffer).ConfigureAwait(false))();

            }
            catch (Exception e)
            {
                Console.Error.WriteLine(e);
            }
            NormalMode();
        }

        void DataWindow_Closing(object sender, CancelEventArgs e)
        {
            Reset();
        }

        private void Set_Click(object sender, RoutedEventArgs evt)
        {
            //Create the request buffer
            var buffer = new byte[65];
            buffer[0] = 0x00; // Report id
            buffer[1] = 0x00; // Instruction - Settings
            buffer[2] = Color.R; // R
            buffer[3] = Color.G; // Gs
            buffer[4] = Color.B; // B
            buffer[5] = 255; // brightnessss
            buffer[6] = 0; // Mode

            try
            {
                //Write and read the data to the device
                new Action(async () => await device.WriteAsync(buffer).ConfigureAwait(false))();

            }
            catch (Exception e)
            {
                Console.Error.WriteLine(e);
            }

            NormalMode();
        }

        private void Rainbow_Click(object sender, RoutedEventArgs evt)
        {
            RainbowMode();
        }

        private void NormalMode()
        {
            var buffer = new byte[65];
            buffer[0] = 0x00; // Report id
            buffer[1] = 0x02; // Instruction - Mode
            buffer[2] = 0; // Normal

            try
            {
                //Write and read the data to the device
                new Action(async () => await device.WriteAsync(buffer).ConfigureAwait(false))();

            }
            catch (Exception e)
            {
                Console.Error.WriteLine(e);
            }
        }

        private void RainbowMode()
        {
            var buffer = new byte[65];
            buffer[0] = 0x00; // Report id
            buffer[1] = 0x02; // Instruction - Mode
            buffer[2] = 1; // Rainbow

            try
            {
                //Write and read the data to the device
                new Action(async () => await device.WriteAsync(buffer).ConfigureAwait(false))();

            }
            catch (Exception e)
            {
                Console.Error.WriteLine(e);
            }
        }

        private void Reset_Click(object sender, RoutedEventArgs evt)
        {
            Reset();
        }
    }
}
