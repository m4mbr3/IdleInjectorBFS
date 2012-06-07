#include<stdio.h>
#include<stdlib.h>
#include<sensors/sensors.h>




int main (void)
{
	FILE *fp = NULL;
	while(sensors_init(fp)!=0)
		sensors_cleanup();
	
	struct sensors_bus_id bus;
	bus.type = SENSORS_BUS_TYPE_I2C;
	bus.nr = SENSORS_BUS_NR_ANY;
	
}

