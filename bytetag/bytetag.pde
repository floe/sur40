// Microsoft Surface Bytetag generator
// after https://msdn.microsoft.com/library/ee804885.aspx
// (CC) 2017 by Martin Kaltenbrunner <martin@tuio.org>

import processing.pdf.*;

PFont font;
PGraphics pdf;
PShape bytetag;
PShape bits[] = new PShape[8];

// 0.75 inch = 19.05mm
// 19.05 * 2.83464567 = 54;
int tag_size = 54; // 0.75 * 72dpi

int border = 30;
int xpos   = border;
int ypos   = border;
 
void setup() {
  
  size(480, 480);
  noLoop();
  
  bytetag = loadShape("bytetag.svg");
  pdf = createGraphics(598, 842, PDF, "bytetag.pdf");
  font = createFont("Arial", 8);  
  
  for (int i=0;i<8;i++) {
    bits[i] = bytetag.getChild("bit"+i);
  }

}

void draw() {
    background(255);
    shape(bytetag, 20, 20, 440, 440); 
} 

void mouseClicked() {
  render();
}

void render() {
  pdf.beginDraw();
  pdf.background(255);
  pdf.textFont(font, 8);
  
  int page = 1;
  int last_page = 0;
  
  for (int id=0;id<256;id++) {
    
    if (page>last_page) {
      println("\ncreating page #"+page);
      pdf.fill(0);
      last_page = page;
    }
    
    String id_bin = binary(id ,8);
    String id_hex = hex(id, 2);
    println("generating ID "+id_hex);
    
    for (int i=0;i<8;i++) {
      if (id_bin.charAt(7-i) == '1') {
        bits[i].setVisible(true);
      } else {
        bits[i].setVisible(false);
      }
    }
 
    pdf.shape(bytetag, xpos, ypos, tag_size, tag_size);
    pdf.text("ID "+id, xpos, ypos+tag_size+border/4);
    pdf.text(id_hex, xpos+3*border/2, ypos+tag_size+border/4);

    xpos+=(border/2+tag_size);
    if ((xpos+border+tag_size)>pdf.width) {
      xpos = border;
      ypos+=(border/2+tag_size);
      
      if ((id+1)%64==0) {
         ypos = border;
         if (id<255) {
           ((PGraphicsPDF)pdf).nextPage();
           pdf.background(255);
           page++;
         }
       }
    }
   
    redraw();
    //delay(250);
  }
  
  pdf.endDraw();
  pdf.dispose();
  println("\nPDF file saved");
}