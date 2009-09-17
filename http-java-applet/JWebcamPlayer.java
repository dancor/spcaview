
/*
 * JWebcamPlayer.java
 *
 * Created on March 21, 2005, 1:31 AM
 */

/**
 *
 * @author Alvaro Salmador (nx5) (naplam33 at msn.com)
 */


import java.applet.*;
import java.applet.AppletContext.*;
import java.awt.*;
import java.awt.event.*;
import java.awt.image.*;
import java.awt.Image;
import java.net.URL;
import javax.swing.*;
//import javax.swing.table.*;
import javax.imageio.*;
//import java.util.*;
import java.io.*;
import java.net.*;



public class JWebcamPlayer extends javax.swing.JApplet implements MouseListener, MouseMotionListener
{
    public static final boolean DEBUGGING = false;
   
    public JWebcamPlayer() 
    {
    }

    public static String checkAppletLoaded()
    {
        return "ok";
    }
    public static int unsignedByteToInt( byte b )
    {
    	return (int) b & 0xff;
    }
    public void init() 
    {
        m_strColor = getParameter("Color");
        if (m_strColor==null || m_strColor=="") m_strColor = "#FFFFFF";

        m_strServer = getCodeBase().getHost();//getParameter("Server");
        if (m_strServer==null || m_strServer=="") m_strServer = "127.0.0.1";

        m_strPort = getParameter("Port");
        if (m_strPort==null || m_strPort=="") m_strPort = "7070";
				
				overlay = toBufferedImage(getImage(getDocumentBase(), "control.jpg" ));
				do_overlay = false;
									
				addMouseListener(this);
				addMouseMotionListener(this);

    }
   
    public void createGUI()
    {
        setBackground(Color.decode(m_strColor));
        
        try {
            //UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName()); //native
            UIManager.setLookAndFeel(UIManager.getCrossPlatformLookAndFeelClassName()); //java
        } catch(Exception e) {  }
 
        Container content = getContentPane();
        
        content.setBackground(Color.decode(m_strColor));
        
        GridBagLayout gbl = new GridBagLayout();
        content.setLayout(gbl);

        GridBagConstraints c = new GridBagConstraints();
        c.fill = GridBagConstraints.BOTH;
        c.weighty = 1.0;
        c.weightx = 1.0;
        c.gridwidth = 1;
        c.anchor = GridBagConstraints.NORTHWEST;
        c.insets = new Insets(0, 0,0,0);
        c.gridheight = 1;
        c.gridx = 0;
        c.gridy = 0;

        m_label = new JLabel();
				m_label.setHorizontalAlignment(SwingConstants.LEFT);
				m_label.setVerticalAlignment(SwingConstants.TOP);

        m_label.setText("JWebcamPlayer Applet");
    
        content.add(m_label, c);
    }

    public void start()
    {
        m_stop = false;
        
        try {
            javax.swing.SwingUtilities.invokeAndWait(new Runnable() 
            {
                public void run() {
                    createGUI();
                }
            } );
        } catch (Exception e) { 
        }

        m_worker = new SwingWorker() {
            public Object construct() {
                
                Socket connection;
                InputStream in;
                OutputStream out;
                try {
                  int port = 0;
                  try {
                    port = Integer.parseInt(m_strPort);
                  }
                  catch(Exception e) {
                    port = 0;
                  }
                  if (port==0) port = 7070;
                  connection = new Socket(m_strServer, port);
                  in = connection.getInputStream();
                  out = connection.getOutputStream();
                }
                catch (IOException e) {
                  System.out.println(
                      "Attempt to create connection failed with error: " + e);
                  m_label.setIcon(null);
                  m_label.setText("JWebcamPlayer - Connection failed");
                  m_stop = true;
                  return null;
                }
                
                byte [] buffer = new byte [512*1024];
                
                m_label.setText("");
        
                while(!m_stop)
                {
                    // bucle, constantemente lee imagen del server y la muestra
                    byte [] b = {'O','K',0,0, 0,0,0,0,0,0,0,0,0};
                    
										if(bright_up) {
											b[7] = 1;
											bright_up = false;
										}
										if(bright_down) {
											b[7] = 2;
											bright_down = false;
										}
										if(contrast_up) {
											b[8] = 1;
											contrast_up = false;
										}
										if(contrast_down) {
											b[8] = 2;
											contrast_down = false;
										}
                    
                    int n = 0;
                    int siz = 0;

                    try {
                        out.write(b);

                        if (DEBUGGING)
                            System.out.print("-sending my header ("+Integer.toString(b.length)+") bytes");
                        
                        n = in.read(buffer, 0, HDRLEN);
                        /*while(n<HDRLEN && n>0) {
                            int r = in.read(buffer, n, HDRLEN-n);
                            if (r<=0) break;
                        }*/
                        
                        if (DEBUGGING)
                            System.out.print("-read header ("+Integer.toString(n)+") bytes");
                    }
                    catch(Exception e) {
                        if (DEBUGGING)
                            e.printStackTrace();
                        
                        m_stop = true;
                    }
                    
                    if (n<HDRLEN) 
                    {
                        if (n<=0) 
                            m_stop = true;

                        continue;
                    } 
                    else 
                    {
                        // can someone point out a more straightforward way to convert 4 bytes
                        // to a java integer? this is just awful...
                        int ssz = SZOFS;
												siz += unsignedByteToInt(buffer[ssz+3]) << 24;
												siz += unsignedByteToInt(buffer[ssz+2]) << 16;
												siz += unsignedByteToInt(buffer[ssz+1]) << 8;
												siz += unsignedByteToInt(buffer[ssz]) ;
                        if (DEBUGGING)
                            System.out.println("** reading jpeg, size="+Integer.toString(siz));

                        n = HDRLEN;
                        if (buffer[0]!='S' || buffer[1]!='P' || buffer[2]!='C' || buffer[3]!='A')
                        {
                            if (DEBUGGING)
                                System.out.println("*Header missing 'SPCA'");
                            
                            continue;
                        }
                        else if (siz<=0 || siz>(512*1024))
                        {
                            siz = 0;
                            try { 
                                in.read(buffer, n, buffer.length); 
                            }   
                            catch(Exception e) {
                                if (DEBUGGING)
                                    e.printStackTrace();
                            }
                            
                            if (DEBUGGING)
                                System.out.println("*Illegal image size");
                            
                            continue;
                        }
                        else 
                        {
                            do
                            {
                                int r = 0;
                                try {
                                    //r = in.read(buffer, n, HDLREN + siz - n);
                                    r = in.read(buffer, n, buffer.length - n);
                                }   
                                catch(Exception e)
                                {
                                    e.printStackTrace();
                                }

                                n += r;

                                if (DEBUGGING)
                                    System.out.println(" chunk read, size="+Integer.toString(r)+" total="+Integer.toString(n));

                                /*if (r<=0) 
                                {
                                    n = r;
                                    break;
                                }*/
                            }
                            while(n<(siz+HDRLEN));
                        }

                        if (DEBUGGING)
                            System.out.println("jpeg read, size="+Integer.toString(n));
                    }
                 
                    byte [] buffer2 = new byte [n];
                    
                    for(int i=0; i<n; i++)
                        buffer2[i] = buffer[i+HDRLEN];

										
										try{
											BufferedImage image = ImageIO.read(new ByteArrayInputStream(buffer2));																					
											if(do_overlay) {
												Graphics2D g = image.createGraphics();
												g.drawImage(image, 0, 0, null);
												g.setComposite(AlphaComposite.getInstance(AlphaComposite.SRC_OVER, 0.4f));
												g.drawImage(overlay, 0, 0, null);
												g.dispose();
											}
																						
											ImageIcon ii = new ImageIcon(image);

//                    ImageIcon ii = new ImageIcon(Toolkit.getDefaultToolkit().createImage(buffer2));
	
											if (ii.getIconHeight()<=1 || ii.getIconWidth()<=1)
											{
													if (DEBUGGING)
															System.out.println("--invalid image got");
													continue;
											}
											else 
											{    
													if (DEBUGGING)
															System.out.println("--VALID image got");
											}
	
											m_label.setIcon(ii);
										}	
										catch (IOException e) {}
                }//while
                
                try {
                  connection.close();
                      // (Alternatively, you might depend on the server
                      //  to close the connection.)
                }
                catch (IOException e) {
                }

                m_label.setIcon(null);
                m_label.setText("JWebcamPlayer - Connection closed");
                
                if (DEBUGGING)
                {
                    if (m_stop)
                        System.out.println("** worker ending (m_stop==true)");
                    else    
                        System.out.println("** worker ending (m_stop==false)");
                }                          
                        
                m_stop = true;
                
                return null;
            }
        };
       
        m_worker.start();  //required for SwingWorker 3
    }
    
    public void stop()
    {
        if (DEBUGGING)
            System.out.println("--stopping worker (applet stop)");
        
        m_stop = true;
    }
 
    // This method returns true if the specified image has transparent pixels
    public static boolean hasAlpha(Image image) {
			// If buffered image, the color model is readily available
      if (image instanceof BufferedImage) {
      	BufferedImage bimage = (BufferedImage)image;
        return bimage.getColorModel().hasAlpha();
      }
    
      // Use a pixel grabber to retrieve the image's color model;
      // grabbing a single pixel is usually sufficient
      PixelGrabber pg = new PixelGrabber(image, 0, 0, 1, 1, false);
      try {
      	 pg.grabPixels();
      } catch (InterruptedException e) { }
    
      // Get the image's color model
      ColorModel cm = pg.getColorModel();
      return cm.hasAlpha();
    }

    // This method returns a buffered image with the contents of an image
    public static BufferedImage toBufferedImage(Image image) {
        if (image instanceof BufferedImage) {
            return (BufferedImage)image;
        }
    
        // This code ensures that all the pixels in the image are loaded
        image = new ImageIcon(image).getImage();
    
        // Determine if the image has transparent pixels; for this method's
        // implementation, see e661 Determining If an Image Has Transparent Pixels
        boolean hasAlpha = hasAlpha(image);
    
        // Create a buffered image with a format that's compatible with the screen
        BufferedImage bimage = null;
        GraphicsEnvironment ge = GraphicsEnvironment.getLocalGraphicsEnvironment();
        try {
            // Determine the type of transparency of the new buffered image
            int transparency = Transparency.OPAQUE;
            if (hasAlpha) {
                transparency = Transparency.BITMASK;
            }
    
            // Create the buffered image
            GraphicsDevice gs = ge.getDefaultScreenDevice();
            GraphicsConfiguration gc = gs.getDefaultConfiguration();
            bimage = gc.createCompatibleImage(
                image.getWidth(null), image.getHeight(null), transparency);
        } catch (HeadlessException e) {
            // The system does not have a screen
        }
    
        if (bimage == null) {
            // Create a buffered image using the default color model
            int type = BufferedImage.TYPE_INT_RGB;
            if (hasAlpha) {
                type = BufferedImage.TYPE_INT_ARGB;
            }
            bimage = new BufferedImage(image.getWidth(null), image.getHeight(null), type);
        }
    
        // Copy image to buffered image
        Graphics g = bimage.createGraphics();
    
        // Paint the image onto the buffered image
        g.drawImage(image, 0, 0, null);
        g.dispose();
    
        return bimage;
    }
		
   	public void mouseClicked(MouseEvent evt) {
		
      int x = evt.getX();  // Location where user clicked.
      int y = evt.getY();
			
			System.out.println("x="+Integer.toString(x)+" y="+Integer.toString(y));
			
			if(do_overlay) {
				//base check
				if(x>0 && x<42 && y>0 && y<42) {
					if(y>2 && y<18) {								//contrast
						
						if(x>0 && x<10) {								//decrement
							System.out.println("c -");
							contrast_down = true;
						}
						else if(x > 30 && x < 40) {			//increment
							System.out.println("c +");
							contrast_up = true;
						}
					} else if(y > 22 && y < 40) {		//brightness
						
						if(x>0 && x<10) {								//decrement
							System.out.println("b -");
							bright_down = true;
						}
						else if(x > 30 && x < 40) {			//increment
							System.out.println("b +");
							bright_up = true;
						}
					}		
				} else {
					do_overlay=false;
				}
			} else {
			 do_overlay=true;
			}
		
		
		}

		
		public void mousePressed(MouseEvent evt) { }
		public void mouseReleased(MouseEvent evt) { }
		public void mouseDragged(MouseEvent evt) { }
   	public void mouseMoved(MouseEvent evt) { }
   	public void mouseEntered(MouseEvent evt) { }
   	public void mouseExited(MouseEvent evt) { }

		   
    private final int HDRLEN = 50;
    private final int SZOFS = 29;
		private BufferedImage overlay = null;
		private boolean do_overlay = false;
    private JLabel m_label = null;
    private String m_strServer = null;
    private String m_strPort = null;
    private String m_strColor = null;
    private boolean m_stop = false;
    private SwingWorker m_worker = null;
		private boolean bright_up = false;
		private boolean bright_down = false;
		private boolean contrast_up = false;
		private boolean contrast_down = false;
		

}



