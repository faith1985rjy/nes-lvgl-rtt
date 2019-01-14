#include <rtthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <dfs_posix.h>
#include <string.h>
#include <sys/types.h>

#include "nes_main.h" 
#include "nes_ppu.h"
#include "nes_mapper.h"
#include "nes_apu.h"

//#define DRV_DEBUG
#define LOG_TAG             "nes.sys.main"
#include <drv_log.h>


// struct NesHeader_tag
// {
//   uint8_t byID[ 4 ];
//   uint8_t byRomSize;
//   uint8_t byVRomSize;
//   uint8_t byInfo1;
//   uint8_t byInfo2;
//   uint8_t byReserve[ 8 ];
// };

// struct NesHeader_tag NesHeader;	

extern volatile uint8_t framecnt;	//nes֡������ 
int MapperNo;			//map���
int NES_scanline;		//nesɨ����
int VROM_1K_SIZE;
int VROM_8K_SIZE;
uint32_t NESrom_crc32;

uint8_t PADdata0;   			//�ֱ�1��ֵ [7:0]��7 ��6 ��5 ��4 Start3 Select2 B1 A0  
uint8_t PADdata1;   			//�ֱ�2��ֵ [7:0]��7 ��6 ��5 ��4 Start3 Select2 B1 A0  
uint8_t *NES_RAM;			//����1024�ֽڶ���
uint8_t *NES_SRAM;  
NES_header *RomHeader; 	//rom�ļ�ͷ
MAPPER *NES_Mapper;		 
MapperCommRes *MAPx;  


uint8_t* spr_ram;			//����RAM,256�ֽ�
ppu_data* ppu;			//ppuָ��
uint8_t* VROM_banks;
uint8_t* VROM_tiles;

apu_t *apu; 			//apuָ��
uint16_t *wave_buffers; 		
uint16_t *saibuf1; 			//��Ƶ����֡,ռ���ڴ��� 367*4 �ֽ�@22050Hz
uint16_t *saibuf2; 			//��Ƶ����֡,ռ���ڴ��� 367*4 �ֽ�@22050Hz

uint8_t* romfile;			//nes�ļ�ָ��,ָ������nes�ļ�����ʼ��ַ.

//����ROM
//����ֵ:0,�ɹ�
//    1,�ڴ����
//    3,map����
uint8_t nes_load_rom(void)
{  
    uint8_t* p;  
	uint8_t i;
	uint8_t ret=0;
	p=(uint8_t*)romfile;
	
	// if(strncmp((char*)p,"NES",3)==0)
	if(memcmp(p,"NES",3)==0)
	{  
		RomHeader->ctrl_z=p[3];
		RomHeader->num_16k_rom_banks=p[4];
		RomHeader->num_8k_vrom_banks=p[5];
		RomHeader->flags_1=p[6];
		RomHeader->flags_2=p[7]; 
		if(RomHeader->flags_1&0x04)p+=512;		//��512�ֽڵ�trainer:
		if(RomHeader->num_8k_vrom_banks>0)		//����VROM,����Ԥ����
		{		
			VROM_banks=p+16+(RomHeader->num_16k_rom_banks*0x4000);
#if	NES_RAM_SPEED==1	//1:�ڴ�ռ��С 0:�ٶȿ�	 
			VROM_tiles=VROM_banks;	 
#else  
			VROM_tiles=malloc(RomHeader->num_8k_vrom_banks*8*1024);//�������������1MB�ڴ�!!!
			if(VROM_tiles==0)VROM_tiles=VROM_banks;//�ڴ治���õ������,����VROM_titles��VROM_banks�����ڴ�			
			compile(RomHeader->num_8k_vrom_banks*8*1024/16,VROM_banks,VROM_tiles);  
#endif	
		}else 
		{
			VROM_banks=malloc(8*1024);
			VROM_tiles=malloc(8*1024);
			if(!VROM_banks||!VROM_tiles)ret=1;
		}  	
		VROM_1K_SIZE = RomHeader->num_8k_vrom_banks * 8;
		VROM_8K_SIZE = RomHeader->num_8k_vrom_banks;  
		MapperNo=(RomHeader->flags_1>>4)|(RomHeader->flags_2&0xf0);
		if(RomHeader->flags_2 & 0x0E)MapperNo=RomHeader->flags_1>>4;//���Ը���λ�����ͷ����������� 
		rt_kprintf("use map:%d\r\n",MapperNo);
		for(i=0;i<255;i++)  // ����֧�ֵ�Mapper��
		{		
			if (MapTab[i]==MapperNo)break;		
			if (MapTab[i]==-1)ret=3; 
		} 
		if(ret==0)
		{
			switch(MapperNo)
			{
				case 1:  
					MAP1=malloc(sizeof(Mapper1Res)); 
					if(!MAP1)ret=1;
					break;
				case 4:  
				case 6: 
				case 16:
				case 17:
				case 18:
				case 19:
				case 21: 
				case 23:
				case 24:
				case 25:
				case 64:
				case 65:
				case 67:
				case 69:
				case 85:
				case 189:
					MAPx=malloc(sizeof(MapperCommRes)); 
					if(!MAPx)ret=1;
					break;  
				default:
					break;
			}
		}
	}
	else
	{
		return RT_ERROR;
	} 
	return ret;	//����ִ�н��
} 

//�ͷ��ڴ� 
void nes_sram_free(void)
{ 
	free(NES_RAM);
	free(NES_SRAM);
	free(RomHeader);
	free(NES_Mapper);
	free(spr_ram);
	free(ppu);
	free(apu);
	free(wave_buffers);
	free(saibuf1);
	free(saibuf2);
	free(romfile);
	if((VROM_tiles!=VROM_banks)&&VROM_banks&&VROM_tiles)//����ֱ�ΪVROM_banks��VROM_tiles�������ڴ�,���ͷ�
	{
		free(VROM_banks);	 
		free(VROM_tiles);
	}
	switch (MapperNo)//�ͷ�map�ڴ�
	{
		case 1: 			//�ͷ��ڴ�			
			free(MAP1);
			break;	 	
		case 4: 
		case 6: 
		case 16:
		case 17:
		case 18:
		case 19:
		case 21:
		case 23:
		case 24:
		case 25:
		case 64:
		case 65:
		case 67:
		case 69:
		case 85:
		case 189:
			free(MAPx);
		default:break; 
	}
	NES_RAM=0;	
	NES_SRAM=0;
	RomHeader=0;
	NES_Mapper=0;
	spr_ram=0;
	ppu=0;
	apu=0;
	wave_buffers=0;
	saibuf1=0;
	saibuf2=0;
	romfile=0; 
	VROM_banks=0;
	VROM_tiles=0; 
	MAP1=0;
	MAPx=0;
} 

//ΪNES���������ڴ�
//romsize:nes�ļ���С
//����ֵ:0,����ɹ�
//       1,����ʧ��
uint8_t nes_sram_malloc(uint32_t romsize)
{	
	NES_RAM = rt_malloc_align(0x800,1024);
 	NES_SRAM=malloc(0X2000);
	RomHeader=malloc(sizeof(NES_header));
	NES_Mapper=malloc(sizeof(MAPPER));
	spr_ram=malloc(0X100);		
	ppu=malloc(sizeof(ppu_data));  
	// apu=malloc(sizeof(apu_t));		//sizeof(apu_t)=  12588
	wave_buffers=malloc(APU_PCMBUF_SIZE*2);
	saibuf1=malloc(APU_PCMBUF_SIZE*4+10);
	saibuf2=malloc(APU_PCMBUF_SIZE*4+10);
 	// romfile=malloc(romsize);			//������Ϸrom�ռ�,����nes�ļ���С  
	if(!NES_RAM||!NES_SRAM||!RomHeader||!NES_Mapper||!spr_ram||!ppu||!apu||!wave_buffers||!saibuf1||!saibuf2||!romfile)
	{
		nes_sram_free();
		return 1;
	}
	memset(NES_SRAM,0,0X2000);				//����
	memset(RomHeader,0,sizeof(NES_header));	//����
	memset(NES_Mapper,0,sizeof(MAPPER));	//����
	memset(spr_ram,0,0X100);				//����
	memset(ppu,0,sizeof(ppu_data));			//����
	// memset(apu,0,sizeof(apu_t));			//����
	memset(wave_buffers,0,APU_PCMBUF_SIZE*2);//����
	memset(saibuf1,0,APU_PCMBUF_SIZE*4+10);	//����
	memset(saibuf2,0,APU_PCMBUF_SIZE*4+10);	//����
	// memset(romfile,0,romsize);				//���� 

	rt_kprintf("sram malloc success.\n");

	return 0;
} 
//��ʼnes��Ϸ
//pname:nes��Ϸ·��
//����ֵ:
//0,�����˳�
//1,�ڴ����
//2,�ļ�����
//3,��֧�ֵ�map
uint8_t nes_load(const char* pname)
{
	// int fd;
	
	uint8_t *buf;			//����
	// uint8_t *p;
	// uint32_t readlen;		//�ܶ�ȡ����
	// uint16_t bread;			//��ȡ�ĳ���
	
	uint8_t ret=0;  
	// if(audiodev.status&(1<<7))//��ǰ�ڷŸ�??
	// {
	// 	audio_stop_req(&audiodev);	//ֹͣ��Ƶ����
	// 	audio_task_delete();		//ɾ�����ֲ�������.
	// }  
	// app_wm8978_volset(wm8978set.mvol);	 
	// WM8978_ADDA_Cfg(1,0);	//����DAC
	// WM8978_Input_Cfg(0,0,0);//�ر�����ͨ��
	// WM8978_Output_Cfg(1,0);	//����DAC���
	
	// buf=malloc(1024);  
	// file=malloc(sizeof(FIL));  
	

	// if(file==0)//�ڴ�����ʧ��.
	// {
	// 	myfree(buf);
	// 	return 1;						  
	// }
	// ret=f_open(file,(char*)pname,FA_READ);

	// fd = open(pname,O_RDONLY | O_BINARY);
	// if (fd < 0)
	// {
	// 	LOG_E("open nes file failed!\n");
	// 	close(fd);
	// 	free(buf);
	// 	return 1;
	// }

	

	FILE *fp;

	/* Open ROM file */
	fp = fopen( pname, "rb" );
	if ( fp == NULL )
		return 1;

	/* Read ROM Header */
	fread( romfile, sizeof RomHeader, 1, fp );
	// if ( memcmp( NesHeader.byID, "NES\x1a", 4 ) != 0 )
	if ( memcmp( romfile, "NES\x1a", 4 ) != 0 )
	{
		/* not .nes file */
		LOG_E("not nes file!\n");
		fclose( fp );
		return 1;
	}
	else
	{
		LOG_I("nes check successful.\n");
	}

	nes_sram_malloc(0);

	/* If trainer presents Read Triner at 0x7000-0x71ff */
	if ( RomHeader->flags_1 & 4 )
	{
		fread( &NES_SRAM[ 0x1000 ], 512, 1, fp );
	}

	romfile = malloc(RomHeader->num_16k_rom_banks * 0x4000);

	// fread( romfile, 0x4000, RomHeader->num_16k_rom_banks, fp );

	// if (RomHeader->num_8k_vrom_banks > 0)
	// {
	// 	/* Allocate Memory for VROM Image */
	// 	VROM_banks = (BYTE *)malloc( NesHeader.byVRomSize * 0x2000 );

	// 	/* Read VROM Image */
	// 	fread( VROM, 0x2000, NesHeader.byVRomSize, fp );
	// }

	// fread( VROM_tiles, 8*1024, RomHeader->num_8k_vrom_banks, fp );

	// fclose(fp);

	// // if(ret!=FR_OK)	//���ļ�ʧ��
	// // {
	// // 	myfree(buf);
	// // 	myfree(file);
	// // 	return 2;
	// // }	 

	// off_t size = lseek(fd, 0, SEEK_END);
	// ret=nes_sram_malloc(size);			//�����ڴ� 

	// // if(ret==0)
	// // {
	// // 	p=romfile;
	// // 	readlen=0;
	// // 	while(readlen<file->obj.objsize)
	// // 	{
	// // 		ret=f_read(file,buf,1024,(UINT*)&bread);//�����ļ�����
	// // 		readlen+=bread;
	// // 		mymemcpy(p,buf,bread); 
	// // 		p+=bread;
	// // 		if(ret)break;
	// // 	}    
	// // 	NESrom_crc32=get_crc32(romfile+16, file->obj.objsize-16);//��ȡCRC32��ֵ	
	// // 	ret=nes_load_rom();						//����ROM
	// // 	if(ret==0) 					
	// // 	{   
	// // 		cpu6502_init();						//��ʼ��6502,����λ	 
	// // 		Mapper_Init();						//map��ʼ�� 	 
	// // 		PPU_reset();						//ppu��λ
	// // 		apu_init(); 						//apu��ʼ�� 
	// // 		nes_sound_open(0,APU_SAMPLE_RATE);	//��ʼ�������豸
	// // 		nes_emulate_frame();				//����NESģ������ѭ�� 
	// // 		nes_sound_close();					//�ر��������
	// // 	}
	// // }

	// LOG_I("size of nes file is:%ld",size);
	// buf = malloc(size+1);
	// if (!buf)
	// {
	// 	LOG_E("malloc for buffer failed!\n");
	// 	goto exit_nes_load_error;
	// }
	
	// // ret = read(fd, buf, size);
	// // if (ret <= 0)
	// // {
	// // 	LOG_E("read nes file to buf error!\n");
	// // 	goto exit_nes_load_error;
	// // }

	// ssize_t ret_r;
	// while (size != 0 && (ret_r = read(fd, buf, size)) != 0)
	// {
	// 	if (ret_r == -1) 
	// 	{
	// 		if (errno == EINTR)
	// 			continue;
	// 		LOG_E("read");
	// 		break;
	// 	}
	// 	size -= ret_r;
	// 	buf += ret_r;
	// }
	// LOG_I("read done");
	// if (memcpy(romfile,buf,size) == RT_NULL)
	// {
	// 	LOG_E("romfile memcpy error!\n");
	// 	goto exit_nes_load_error;
	// }
	

	

	// LOG_I("memcpy done");
	// LOG_I("buf[0-2]:%d %d %d",buf[0],buf[1],buf[2]);
	// LOG_I("romfile[0-2]:%d %d %d",romfile[0],romfile[1],romfile[2]);
	// LOG_I("NES:%d %d %d",'N','E','S');

	// NESrom_crc32=get_crc32(romfile+16, size-16);//��ȡCRC32��ֵ
	// LOG_I("get_crc32 done,NESrom_crc32:%d",NESrom_crc32);

	ret = nes_load_rom();
	if (ret == RT_ERROR)
		LOG_E("nes_load_rom error!");
	// LOG_I("nes_load_rom done");
	// if(ret==0) 					
	// {   
	// 	// cpu6502_init();						//��ʼ��6502,����λ
	// 	Mapper_Init();						//map��ʼ�� 	 
	// 	PPU_reset();						//ppu��λ
	// 	// apu_init(); 						//apu��ʼ�� 
	// 	// nes_sound_open(0,APU_SAMPLE_RATE);	//��ʼ�������豸
	// 	// nes_emulate_frame();				//����NESģ������ѭ�� 
	// 	// nes_sound_close();					//�ر��������
	// }
	// LOG_I("init done");
	
	// close(fd);
	// free(buf);
	// // nes_sram_free();	//�ͷ��ڴ�
	// LOG_I("free done");
	fclose(fp);
	return ret;

exit_nes_load_error:
	// close(fd);
	fclose(fp);
	free(buf);
	nes_sram_free();	//�ͷ��ڴ�
	return RT_ERROR;
}  
uint16_t nes_xoff=0;	//��ʾ��x�᷽���ƫ����(ʵ����ʾ���=256-2*nes_xoff)
uint16_t nes_yoff=0;	//��ʾ��y�᷽���ƫ����

//RGB����Ҫ��3������
//����4��,�������㷽��(800*480Ϊ��):
//offset=lcdltdc.pixsize*(lcdltdc.pwidth*(lcdltdc.pheight-(i-sx)*2-1)+nes_yoff+LineNo) 
//offset=2*(800*(480+(sx-i)*2-1)+nes_yoff+LineNo)
//      =1600*(480+(sx-i)*2-1)+2*nes_yoff+LineNo*2
//      =766400+3200*(sx-i)+2*nes_yoff+LineNo*2 
//nes_rgb_parm1=766400
//nes_rgb_parm2=3200
//nes_rgb_parm3=nes_rgb_parm2/2

//������,�������㷽��(480*272Ϊ��):
//offset=lcdltdc.pixsize*(lcdltdc.pwidth*(lcdltdc.pheight-(i-sx)-1)+nes_yoff+LineNo*2) 
//offset=2*(480*(272+sx-i-1)+nes_yoff+LineNo*2)
//      =960*(272+sx-i-1)+2*nes_yoff+LineNo*4
//      =260160+960*(sx-i)+2*nes_yoff+LineNo*4 
//nes_rgb_parm1=260160
//nes_rgb_parm2=960
//nes_rgb_parm3=nes_rgb_parm2/2

uint32_t nes_rgb_parm1;
uint16_t nes_rgb_parm2;
uint16_t nes_rgb_parm3;

// //������Ϸ��ʾ����
// void nes_set_window(void)
// {	
// 	uint16_t xoff=0,yoff=0; 
// 	uint16_t lcdwidth,lcdheight;
// 	if(lcddev.width==240)
// 	{
// 		lcdwidth=240;
// 		lcdheight=240;
// 		nes_xoff=(256-lcddev.width)/2;	//�õ�x�᷽���ƫ����
// 	}else if(lcddev.width<=320) 
// 	{
// 		lcdwidth=256;
// 		lcdheight=240; 
// 		nes_xoff=0;
// 	}else if(lcddev.width>=480)
// 	{
// 		lcdwidth=480;
// 		lcdheight=480; 
// 		nes_xoff=(256-(lcdwidth/2))/2;//�õ�x�᷽���ƫ����
// 	}
// 	xoff=(lcddev.width-lcdwidth)/2;  
// 	if(lcdltdc.pwidth)//RGB��
// 	{
// 		if(lcddev.id==0X4342)nes_rgb_parm2=lcddev.height*2;
// 		else nes_rgb_parm2=lcddev.height*2*2; 
// 		nes_rgb_parm3=nes_rgb_parm2/2;
// 		if(lcddev.id==0X4342)nes_rgb_parm1=260160-nes_rgb_parm2*xoff; 
// 		else if(lcddev.id==0X7084)nes_rgb_parm1=766400-nes_rgb_parm3*xoff; 
// 		else if(lcddev.id==0X7016||lcddev.id==0X1016)nes_rgb_parm1=1226752-nes_rgb_parm3*xoff; 
// 		else if(lcddev.id==0X1018)nes_rgb_parm1=2045440-nes_rgb_parm3*xoff; 
// 	}
// 	yoff=(lcddev.height-lcdheight-gui_phy.tbheight)/2+gui_phy.tbheight;//��Ļ�߶� 
// 	nes_yoff=yoff;
// 	LCD_Set_Window(xoff,yoff,lcdwidth,lcdheight);//��NESʼ������Ļ����������ʾ
// 	LCD_SetCursor(xoff,yoff);
// 	LCD_WriteRAM_Prepare();//д��LCD RAM��׼��  
// }
extern void KEYBRD_FCPAD_Decode(uint8_t *fcbuf,uint8_t mode);
// //��ȡ��Ϸ�ֱ�����
// void nes_get_gamepadval(void)
// {  
// 	uint8_t *pt;
// 	while((usbx.bDeviceState&0XC0)==0X40)//USB�豸������,���ǻ�û���ӳɹ�,�Ͳ�ѯ.
// 	{
// 		usbapp_pulling();	//��ѯ����USB����
// 	}
// 	usbapp_pulling();		//��ѯ����USB����
// 	if(usbx.hdevclass==4)	//USB��Ϸ�ֱ�
// 	{	
// 		PADdata0=fcpad.ctrlval;
// 		PADdata1=0;
// 	}else if(usbx.hdevclass==3)//USB����ģ���ֱ�
// 	{
// 		KEYBRD_FCPAD_Decode(pt,0);
// 		PADdata0=fcpad.ctrlval;
// 		PADdata1=fcpad1.ctrlval; 
// 	}	
// }    
// //nesģ������ѭ��
// void nes_emulate_frame(void)
// {  
// 	uint8_t nes_frame; 
// 	// TIM8_Int_Init(10000-1,21600-1);//����TIM8,1s�ж�һ��	
// 	nes_set_window();//���ô���
// 	while(1)
// 	{	
// 		// LINES 0-239
// 		PPU_start_frame();
// 		for(NES_scanline = 0; NES_scanline< 240; NES_scanline++)
// 		{
// 			run6502(113*256);
// 			NES_Mapper->HSync(NES_scanline);
// 			//ɨ��һ��		  
// 			if(nes_frame==0)scanline_draw(NES_scanline);
// 			else do_scanline_and_dont_draw(NES_scanline); 
// 		}  
// 		NES_scanline=240;
// 		run6502(113*256);//����1��
// 		NES_Mapper->HSync(NES_scanline); 
// 		start_vblank(); 
// 		if(NMI_enabled()) 
// 		{
// 			cpunmi=1;
// 			run6502(7*256);//�����ж�
// 		}
// 		NES_Mapper->VSync();
// 		// LINES 242-261    
// 		for(NES_scanline=241;NES_scanline<262;NES_scanline++)
// 		{
// 			run6502(113*256);	  
// 			NES_Mapper->HSync(NES_scanline);		  
// 		}	   
// 		end_vblank(); 
// 		nes_get_gamepadval();//ÿ3֡��ѯһ��USB
// 		apu_soundoutput();//�����Ϸ����	 
// 		framecnt++; 	
// 		nes_frame++;
// 		if(nes_frame>NES_SKIP_FRAME)nes_frame=0;//��֡ 
// 		if(system_task_return)break;//TPAD����
// 		if(lcddev.id==0X1963)//����1963,ÿ����һ֡,��Ҫ���贰��
// 		{
// 			nes_set_window();
// 		} 
// 	}
// 	LCD_Set_Window(0,0,lcddev.width,lcddev.height);//�ָ���Ļ����
// 	TIM8->CR1&=~(1<<0);//�رն�ʱ��8
// }
//��6502.s���汻����
void debug_6502(uint16_t reg0,uint8_t reg1)
{
	printf("6502 error:%x,%d\r\n",reg0,reg1);
}
////////////////////////////////////////////////////////////////////////////////// 	 
// //nes,��Ƶ���֧�ֲ���
// vuint8_t nestransferend=0;	//sai������ɱ�־
// vuint8_t neswitchbuf=0;		//saibufxָʾ��־
// //SAI��Ƶ���Żص�����
// void nes_sai_dma_tx_callback(void)
// {  
// 	if(DMA2_Stream3->CR&(1<<19))neswitchbuf=0; 
// 	else neswitchbuf=1;  
// 	nestransferend=1;
// }
// //NES����Ƶ���
// int nes_sound_open(int samples_per_sync,int sample_rate) 
// {
// 	printf("sound open:%d\r\n",sample_rate);
// 	WM8978_ADDA_Cfg(1,0);	//����DAC
// 	WM8978_Input_Cfg(0,0,0);//�ر�����ͨ��
// 	WM8978_Output_Cfg(1,0);	//����DAC���  
// 	WM8978_I2S_Cfg(2,0);	//�����ֱ�׼,16λ���ݳ���
// 	app_wm8978_volset(wm8978set.mvol);
// 	SAIA_Init(0,1,4);		//����SAI,������,16λ���� 
// 	SAIA_SampleRate_Set(sample_rate);		//���ò�����
// 	SAIA_TX_DMA_Init((uint8_t*)saibuf1,(uint8_t*)saibuf2,2*APU_PCMBUF_SIZE,1);//DMA���� 
//  	sai_tx_callback=nes_sai_dma_tx_callback;//�ص�����ָwav_sai_dma_callback
// 	SAI_Play_Start();						//����DMA    
// 	return 1;
// }
// //NES�ر���Ƶ���
// void nes_sound_close(void) 
// { 
// 	SAI_Play_Stop();
// 	app_wm8978_volset(0);				//�ر�WM8978�������
// } 
// //NES��Ƶ�����SAI����
// void nes_apu_fill_buffer(int samples,uint16_t* wavebuf)
// {	
//  	int i;	 
// 	while(!nestransferend)//�ȴ���Ƶ�������
// 	{
// 		delay_ms(5);
// 	}
// 	nestransferend=0;
//     if(neswitchbuf==0)
// 	{
// 		for(i=0;i<APU_PCMBUF_SIZE;i++)
// 		{
// 			saibuf1[2*i]=wavebuf[i];
// 			saibuf1[2*i+1]=wavebuf[i];
// 		}
// 	}else 
// 	{
// 		for(i=0;i<APU_PCMBUF_SIZE;i++)
// 		{
// 			saibuf2[2*i]=wavebuf[i];
// 			saibuf2[2*i+1]=wavebuf[i];
// 		}
// 	}
// } 



















