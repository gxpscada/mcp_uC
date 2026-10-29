// Reading the txt file---------------------------------------------------------
pointCnt = 3000;
rawADC = read("C:\Develop\STM32\pt100\LOGs\capt_pt1000_heating_water0_chest.txt",pointCnt,1);
t = linspace(0,1,pointCnt);

function y = MedianFilter(x, n)
    // x — входной вектор, n — порядок (длина) окна (рекомендуется нечетное число)
    if nargin < 2 then, n = 3; end
    
    L = length(x);
    y = zeros(1, L);
    half_n = floor(n / 2);
    
    for i = 1:L
        // Определяем границы окна с учетом краев сигнала
        left = max(1, i - half_n);
        right = min(L, i + half_n);
        
        // Вычисляем медиану для текущего окна
        y(i) = median(x(left:right));
    end
endfunction

// Median Filter [window = 3]
rawMedian = MedianFilter(rawADC,3);

function result = MovAveFilter(data, N)
    // data - исходный одномерный массив (вектор-строка или вектор-столбец)
    // N - размер окна усреднения
    
    // Создаем коэффициенты фильтра (массив из N элементов, равных 1/N)
    b = ones(1, N) / N;
    
    // Применяем фильтр к исходным данным
    result = filter(b, 1, data);
    
    // Опционально: первые N-1 элементов будут «разогревочными» (неполное окно).
    // Если нужно заменить их на %nan, раскомментируйте строчку ниже:
    result(1:N-1) = %nan;
endfunction

// Moving Average [window = 4] filter
rawMovAve = MovAveFilter(rawMedian,6);

// Исходные данные
scf(0); // Создает окно №0 (или переключается на него)
clf();  // Очищает окно перед рисованием
plot(t, rawADC,t,rawMedian,t,rawMovAve);
xlabel('Time');xlabel color blue fontsize 3;//Define x axis
ylabel('ADC');ylabel color blue fontsize 3;//Define y axis
title("Reading a raw ADC pt1000 data");
legend("raw ADC","Median [3] fliterd","Run Average [6] filtered");
gcf().background=color('gray');//Set the background color of the figure
gca().background=color('gray85');//Set the background color of the plotting area


// Consts For Calculations
R0 = 1000.0;
Rref = 4020.0;
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

function t = getCVDTemper(Res)
//    D = (R0*A)*(R0*A) - 4*(R0*B)*(R0-Res);
    ratio = Res / R0;
    D = A*A - 4*B*(1-(ratio));
//    t = (-R0*A - sqrt(D))/(2*R0*B);
    t = (sqrt(D)-A)/(2*B);
endfunction



Resistance = getResistance(rawMovAve);
//Resistance2 = getResistance(rawADC2);

linTemper = getTemper(Resistance);
//Temp2 = getTemper(Resistance2);

cvdTemper = getCVDTemper(Resistance);
//KVDTemp2 = getKVDTemper(Resistance2);

//Bcorr = -2.185;
linB = 0.2145;
linK =1;

cvdB = 0.2115;
cvdK =1;

function ct = correctT(T,K,B)
    ct = K*T+B;
endfunction

CorrLinTemper = correctT(linTemper,linK,linB);
CorrCvdTemper = correctT(cvdTemper,cvdK,cvdB);

scf(1); // Создает окно №0 (или переключается на него)
subplot(1, 2, 1);
plot(t,CorrLinTemper);
xgrid(2,1,8);
xlabel('Time');xlabel color blue fontsize 3;//Define x axis
ylabel('Linear Temperature');ylabel color blue fontsize 3;//Define y axis
title("Linear Temperature");
legend("Linear Temp");
gcf().background=color('gray');//Set the background color of the figure
gca().background=color('gray85');//Set the background color of the plotting area
// colors names are coming from Color_list in SCILAB help browser

//scf(2); // Создает окно №0 (или переключается на него)
subplot(1, 2, 2);
plot(t,CorrCvdTemper);
xgrid(2,1,8);
xlabel('Time');xlabel color blue fontsize 3;//Define x axis
ylabel('Temperature');ylabel color blue fontsize 3;//Define y axis
title("CVD Temperature");
legend("CVD Temp");
gcf().background=color('gray');//Set the background color of the figure
gca().background=color('gray85');//Set the background color of the plotting area
// colors names are coming from Color_list in SCILAB help browser
