// Reading the txt file---------------------------------------------------------
pointCnt = 5000;
rawADC1=read("C:\Develop\STM32\pt100\LOGs\capture_0_2.txt",pointCnt,1);
//rawADC2=read("C:\Develop\STM32\pt100\LOGs\capture1.txt",pointCnt,1);
t = linspace(0,1,pointCnt);

R0 = 100.0;
Rref = 431.3;
ADCmax = 32768.0;
Pt_Alpha = 0.003851;

A = 3.9083e-3;
B = -5.775e-7;
C = -4.183e-12;

//float resistance = ((float)raw_rtd * MAX31865_RREF) / MAX31865_maxVal;
function y = getResistance(adc)
//    y = (adc*Rref)/ADCmax;
    y = (adc/ADCmax)*Rref;
endfunction

// float t = (resistance - MAX31865_R0) / (MAX31865_R0 * Pt_alpha);
function temp = getTemper(Res)
    temp = (Res-R0)/(R0*Pt_Alpha); 
endfunction

function t = getKVDTemper(Res)
//    D = (R0*A)*(R0*A) - 4*(R0*B)*(R0-Res);
    ratio = Res / R0;
    D = A*A - 4*B*(1-(ratio));
//    t = (-R0*A - sqrt(D))/(2*R0*B);
    t = (sqrt(D)-A)/(2*B);
endfunction

Resistance1 = getResistance(rawADC1);
//Resistance2 = getResistance(rawADC2);

Temp1 = getTemper(Resistance1);
//Temp2 = getTemper(Resistance2);

KVDTemp1 = getKVDTemper(Resistance1);
//KVDTemp2 = getKVDTemper(Resistance2);

// 2. Задание коэффициентов фильтра
// 2. Задаем размер окна фильтра (например, 5)
window_size = 25;

// 3. Реализация фильтра скользящего среднего через свертку (КИХ-фильтр)
// Коэффициенты фильтра: равны 1 / window_size для каждого элемента окна
b = ones(1, window_size) / window_size;
filteredTemp1 = convol(b, Temp1);                 // Применяем фильтр

// Отрезаем лишние краевые эффекты от свертки для совпадения размеров
trimmedTemp1 = filteredTemp1(floor(window_size/2) : length(Temp1) + floor(window_size/2) - 1);


Bcorr = -2.185;
Kcorr =1;

function ct = correctT(T)
    ct = Kcorr*T+Bcorr;
endfunction

CorrTemp1 = correctT(Temp1);
CorrTrimmedTemp1 = correctT(trimmedTemp1);


clf;
plot(t,CorrTemp1,t,CorrTrimmedTemp1);
//plot(t,Temp1,t,KVDTemp1,t,CorrTemp1);
//plot(t,Temp1,t,KVDTemp1);
//plot(t,Temp1,t,Temp2,t,KVDTemp1,t,KVDTemp2);
xgrid(2,1,8);
xlabel('Time');xlabel color blue fontsize 3;//Define x axis
ylabel('Temperature');ylabel color blue fontsize 3;//Define y axis
title("Reading a raw ADC pt100 data");
legend("Temp simple","Temp KVD");
gcf().background=color('gray');//Set the background color of the figure
gca().background=color('gray85');//Set the background color of the plotting area
// colors names are coming from Color_list in SCILAB help browser

