#include "ancs_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t attribute(uint8_t *p, uint8_t id, const char *value)
{
    size_t n=strlen(value);
    p[0]=id; p[1]=n&255; p[2]=n>>8;
    memcpy(p+3,value,n);
    return n+3;
}
int main(void)
{
    uint8_t command[14];
    const uint8_t expected[]={0,0x78,0x56,0x34,0x12,1,128,0,2,128,0,3,128,1};
    assert(ancs_request(command,sizeof(command),0x12345678,true)==sizeof(expected));
    assert(memcmp(command,expected,sizeof(expected))==0);
    assert(ancs_request(command,13,1,true)==0);
    assert(ancs_request(command,sizeof(command),0x12345678,false)==6 && command[5]==0);

    ancs_event_t event;
    const uint8_t ns[]={1,0,11,1,0x78,0x56,0x34,0x12};
    assert(ancs_decode_event(ns,sizeof(ns),&event));
    assert(event.uid==0x12345678 && event.event==ANCS_MODIFIED);
    assert(!ancs_decode_event(ns,7,&event));
    assert(!ancs_decode_event(ns,9,&event));
    uint8_t removed[]={2,0,0,0,0,0,0,0};
    assert(ancs_decode_event(removed,8,&event) && event.event==ANCS_REMOVED);
    removed[0]=3;
    assert(!ancs_decode_event(removed,8,&event));

    uint8_t data[1024]={0,0x78,0x56,0x34,0x12};
    size_t n=5;
    n+=attribute(data+n,ANCS_TITLE,"Rẽ phải sau 200 m");
    n+=attribute(data+n,ANCS_SUBTITLE,"");
    n+=attribute(data+n,ANCS_MESSAGE,"Đường Nguyễn Huệ");
    /* Every possible boundary, including a split UTF-8 code point or TLV header. */
    for (size_t split=0;split<=n;split++) {
        ancs_response_t r;
        ancs_response_begin(&r,0x12345678,true);
        int first=ancs_response_feed(&r,data,split);
        if (split<n) {
            assert(first==0);
            assert(ancs_response_feed(&r,data+split,n-split)==1);
        } else assert(first==1);
        char text[385];
        assert(ancs_response_text(&r,ANCS_TITLE,text,sizeof(text)));
        assert(strcmp(text,"Rẽ phải sau 200 m")==0);
        assert(ancs_response_text(&r,ANCS_SUBTITLE,text,sizeof(text)) && text[0]==0);
        assert(ancs_response_text(&r,ANCS_MESSAGE,text,sizeof(text)));
        assert(strcmp(text,"Đường Nguyễn Huệ")==0);
        assert(!ancs_response_text(&r,ANCS_MESSAGE,text,3));
    }
    ancs_response_t r;
    ancs_response_begin(&r,0x12345678,true);
    for (size_t i=0;i<n;i++) assert(ancs_response_feed(&r,data+i,1)==(i==n-1 ? 1 : 0));
    assert(ancs_response_feed(&r,data,1)==-1); /* No trailing frame accepted. */
    ancs_response_begin(&r,99,true);
    assert(ancs_response_feed(&r,data,n)==-1); /* Wrong UID. */
    ancs_response_begin(&r,0x12345678,true);
    assert(ancs_response_feed(&r,data,n-1)==0); /* Partial payload stays incomplete. */
    ancs_response_begin(&r,0x12345678,true);
    data[n]=0;
    assert(ancs_response_feed(&r,data,n+1)==-1);
    ancs_response_begin(&r,0x12345678,true);
    data[5]=0; /* Unrequested attribute. */
    assert(ancs_response_feed(&r,data,n)==-1);
    data[5]=1; data[6]=0xff; data[7]=0xff;
    ancs_response_begin(&r,0x12345678,true);
    assert(ancs_response_feed(&r,data,8)==-1); /* Declared oversized attribute. */
    ancs_response_begin(&r,0x12345678,true);
    assert(ancs_response_feed(&r,data,sizeof(data)+1)==-1); /* Checked before memcpy. */
    n=5;
    n+=attribute(data+n,ANCS_TITLE,"first");
    n+=attribute(data+n,ANCS_TITLE,"duplicate");
    ancs_response_begin(&r,0x12345678,true);
    assert(ancs_response_feed(&r,data,n)==-1);
    n=5;
    n+=attribute(data+n,ANCS_APP,"com.google.Maps");
    ancs_response_begin(&r,0x12345678,false);
    assert(ancs_response_feed(&r,data,n)==1);
    char app[256];
    assert(ancs_response_text(&r,ANCS_APP,app,sizeof(app)));
    assert(strcmp(app,"com.google.Maps")==0);
    puts("ANCS protocol tests passed: framing, UTF-8, limits, UID, added/modified/removed, malformed input");
}
