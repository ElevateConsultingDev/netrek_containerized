/* Does the bold face keep the same advance as regular? That is the property
 * the player list columns depend on. */
#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
int main(void){
    TTF_Init();
    const char *p="/System/Library/Fonts/Menlo.ttc";
    int sz=20, w_r=0,w_b=0,w_syn=0,h;
    char probe[]="MMMMMMMMMMMMMMMMMMMM";
    TTF_Font *r=TTF_OpenFont(p,sz);
    TTF_Font *b=TTF_OpenFontIndex(p,sz,1);
    TTF_Font *syn=TTF_OpenFont(p,sz);
    if(!r){printf("no regular\n");return 1;}
    TTF_SizeText(r,probe,&w_r,&h);
    if(b) TTF_SizeText(b,probe,&w_b,&h);
    if(syn){TTF_SetFontStyle(syn,TTF_STYLE_BOLD);TTF_SizeText(syn,probe,&w_syn,&h);}
    printf("20 chars at size %d:\n",sz);
    printf("  regular face      : %d px\n",w_r);
    printf("  real bold (idx 1) : %d px  %s\n",w_b,w_b==w_r?"MATCHES":"DIFFERS");
    printf("  synthetic bold    : %d px  %s  <- the old behaviour\n",
           w_syn,w_syn==w_r?"matches":"DIFFERS");
    printf("  drift over 20 chars from synthetic bold: %d px\n", w_syn-w_r);
    TTF_Quit();
    return 0;
}
