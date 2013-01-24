#include "HRV1Analyzer.h"
#include <QDebug>

HRV1Analyzer::HRV1Analyzer() { }

HRV1Analyzer::~HRV1Analyzer() {

}

void HRV1Analyzer::runModule(const ECGRs & r_peaks_data, ECGHRV1 & hrv1_data) {
	this->rPeaksData = r_peaks_data;
	this->hrv1Data = &hrv1_data;

	prepareSignal();
	calculateParameters();
}

/**
 * Przeksztalcenie tablicy z numerami probek na tablice zawierajaca zbior interwalow
 * pomiedzy kolejnymi zespolami RR
 */
void HRV1Analyzer::prepareSignal() {

		signalSize = rPeaksData.GetRs()->signal->size;
		signalSampling = 360;

		 //wlasciwe przygotowanie sygnalu wejsciowego (double *sig)
		#ifdef DEV

	        ExampleSignal es = ExampleSignal();

	        sig = es.getSignal();
	        signalSize = es.getLength();
	        signalSampling = 500;

			#ifdef DEBUG
				qDebug() << "ExampleSignal loaded.";
			#endif

		#else

			sig = new double[signalSize-1];

			double scalingValue = (double)((1/signalSampling)*1000);
			int v1, v2;

			for(int i = 0; i < signalSize-1; i++) {
				v1 = gsl_vector_int_get (rPeaksData.GetRs()->signal, i);
				v2 = gsl_vector_int_get (rPeaksData.GetRs()->signal, i+1);

				sig[i] = (double) abs(v2-v1) * scalingValue;
			}

		#endif

		#ifdef DEBUG
			qDebug() << "Preparing input signal data has been completed.";
		#endif

	}

/**
 * Metoda odpowiadajaca za wyliczenie wszystkich parametrow ilosciowych oraz czestotliwosciowych,
 * ktore skladaja sie na analize, ktora ma przeprowadzic modul HRV1
 */
void HRV1Analyzer::calculateParameters() {

	#ifdef DEBUG
		qDebug() << "HRV1 calculateParameters method started";
	#endif

    double temp=0;

	/////////////////////RR_avg
	//OK: accuracy (vs Matlab 2010b) 0.00005

	this->hrv1Data->RR_avg = mean(sig,0,signalSize);

	/////////////////////RR_stddev
    //OK: accuracy 0.006

	this->hrv1Data->RR_stddev = std(sig,0,signalSize);


	//////////////////////SDNN
	//OK: accuracy 0.00005

	temp=0;

	for(int i=0; i<signalSize; i++) {
		temp += (this->hrv1Data->RR_avg-sig[i])
						*(this->hrv1Data->RR_avg-sig[i]);
	}

	this->hrv1Data->SDNN = sqrt( (double)temp/signalSize );


	//////////////////////RMSSD
	//OK: accuracy 0.00005

	temp=0;

	for(int i=0; i<signalSize-1; i++) {
		temp += (sig[i+1]-sig[i])*(sig[i+1]-sig[i]);
	}

	this->hrv1Data->RMSSD = sqrt( (double)temp/(signalSize-1) );


	//////////////////////NN50
	//OK: accuracy 0.0

	temp=0;
	double thresholdNN50  = 50 * MULTIPLIER;

	for(int i=0; i<signalSize-1; i++) {
		if (abs(sig[i+1]-sig[i]) > thresholdNN50)
			temp++;
	}

	this->hrv1Data->NN50 = temp;


	/////////////////////pNN50
	//OK: accuracy 0.00005

	this->hrv1Data->pNN50 = this->hrv1Data->NN50*100/(signalSize-1);


	/////////////////////SDANN & SDANNi
	//SDANN: accuracy 0.5
	//SDANNi OK: accuracy 0.05

	temp=0;

    //sigAbsolute
	double* sigAbsolute = new double[signalSize];
	sigAbsolute[0] = sig[0];

	for(int i=1; i<signalSize; i++) {
        sigAbsolute[i] = sig[i] + sigAbsolute[i-1];
	}

    //temp variables
    int windowSize = signalSampling*60*5; /* 60 seconds*5minutes */
	int numberOfSteps = std::floor( sigAbsolute[signalSize-1]/windowSize );

    double * mRRI = new double[numberOfSteps];
    double * stdRR5 = new double[numberOfSteps];

	for(int step=1;step<=numberOfSteps;step++) {
	    int windowStartTime = (step-1) * windowSize;
        int windowEndTime = step * windowSize;

        int windowStartIndex = 0;
        int windowEndIndex = 0;

	    for(int i=0; i<signalSize; i++) {
	        if(windowStartTime<=sigAbsolute[i]) {
                windowStartIndex=i;
                break;
	        }
	    }
	    for(int i=0; i<signalSize; i++) {
	        if(windowEndTime<=sigAbsolute[i]) {
                windowEndIndex=i;
                break;
	        }
	    }

	    mRRI[step-1] = mean(sig, windowStartIndex, windowEndIndex+1);
	    stdRR5[step-1] = std(sig, windowStartIndex, windowEndIndex+1);

	    /*//debug
	    std::cout << "windowStartIndex=" << windowStartIndex << " windowEndIndex=" << windowEndIndex << std::endl;
        std::cout << "windowStartTime=" << windowStartTime << " windowEndTime=" << windowEndTime << std::endl;
	    std::cout << "mRRI=" << mRRI[step-1] << " stdRR5=" << stdRR5[step-1] << std::endl;
	    */

	}

	this->hrv1Data->SDANN = std(mRRI, 0, numberOfSteps);
	this->hrv1Data->SDANN_index = mean(stdRR5, 0, numberOfSteps);


	///////////////////SDSD
	//OK: accuracy 0.01

    double* tmpSig = new double[signalSize-1];

    for(int i=0; i<signalSize-1; i++) {
        tmpSig[i] = sig[i+1] - sig[i];
    }

    this->hrv1Data->SDSD = std(tmpSig,0,signalSize-1);

    delete[] tmpSig;


	////////////////////FFT

	double* sigAfterSpline = cubicSpline(sigAbsolute, sig, signalSize);


    int i,size = (int)(sigAbsolute[signalSize-1]/200) +1;
    int isinverse = 1;

    kiss_fft_cpx * out_cpx = new kiss_fft_cpx[size], * out = new kiss_fft_cpx[size], *cpx_buf;

    kiss_fftr_cfg fft = kiss_fftr_alloc(size*2 ,0 ,0,0);
    kiss_fftr_cfg ifft = kiss_fftr_alloc(size*2,isinverse,0,0);

    cpx_buf = copycpx(sigAfterSpline,size);
    kiss_fftr(fft,(kiss_fft_scalar*)cpx_buf, out_cpx);
    kiss_fftri(ifft,out_cpx,(kiss_fft_scalar*)out );

    /*printf("Input:    \tOutput:\n");
    for(i=0;i<size;i++)
    {
        std::cout << "   " << out_cpx[i].r << std::endl;
        buf[i] = (out[i].r)/(size*2);
        printf("%f\t%f\n", sig[i],buf[i]);
    }*/

    kiss_fft_cleanup();
    free(fft);
    free(ifft);

    int sizeFftIndex = (size+(size%2))/2;
    double * fftMagnitude = new double[sizeFftIndex];

    for(int i=0; i<sizeFftIndex; i++) {
        fftMagnitude[i] = 2*(out_cpx[i].r*out_cpx[i].r)/(size*size); //=(abs(out_cpx[i].r)/size)*(abs(out_cpx[i].r)/size)
    }
    // odd nfft excludes Nyquist point
    fftMagnitude[0] = fftMagnitude[0]/2;
    if(size%2==0) {
        fftMagnitude[sizeFftIndex-1] = fftMagnitude[sizeFftIndex-1]/2;
    }

    /*for(i=0;i<sizeFftIndex;i++)
    {
        std::cout << "   " << fftMagnitude[i] << std::endl;
    }*/

    for(int i = 1; i< sizeFftIndex; i++) {
        if(i<=0.003*signalSampling) {
            this->hrv1Data->ULF += fftMagnitude[i];
        }
        else if(i>0.003*signalSampling && i<=0.04*signalSampling) {
            this->hrv1Data->VLF += fftMagnitude[i];
        }
        else if(i>0.04*signalSampling && i<=0.15*signalSampling) {
            this->hrv1Data->LF += fftMagnitude[i];
        }
        else if(i>0.15*signalSampling && i<=0.4*signalSampling) {
            this->hrv1Data->HF += fftMagnitude[i];
        }
    }
    this->hrv1Data->TP = this->hrv1Data->ULF + this->hrv1Data->VLF + this->hrv1Data->LF + this->hrv1Data->HF;
    this->hrv1Data->LFHF = this->hrv1Data->LF / this->hrv1Data->HF;

    delete[] sigAbsolute;

    ////////////////////Sygnaly do wyswietlenia

    //OtherSignal freqency;
    //OtherSignal power;

    this->hrv1Data->freqency = OtherSignal(new WrappedVector);

    this->hrv1Data->power = OtherSignal(new WrappedVector);

    for(int i=0; i<sizeFftIndex; i++) {
    	this->hrv1Data->freqency->set(i, i);
    	this->hrv1Data->power->set(i, fftMagnitude[i]);
    }
	delete mRRI;
	delete stdRR5;
	delete out_cpx;
	delete out;
	delete fftMagnitude;
}

#ifndef DEV
    void HRV1Analyzer::setParams(ParametersTypes &parameterTypes) { }
#endif

kiss_fft_cpx* HRV1Analyzer::copycpx(double *mat, int nframe) {
	int i;
	kiss_fft_cpx *mat2;
	mat2=(kiss_fft_cpx*)KISS_FFT_MALLOC(sizeof(kiss_fft_cpx)*nframe);
        kiss_fft_scalar zero;
        memset(&zero,0,sizeof(zero) );
	for(i=0; i<nframe ; i++) {
		mat2[i].r = mat[i];
		mat2[i].i = zero;
	}
	return mat2;
}

double* HRV1Analyzer::cubicSpline(double* x, double* y, int nframe) {
    //magnet::math::Spline spline;
    int FREQ_SAMPLING = 1000 * 1/5; //bo milisekundy

    alglib::spline1dinterpolant s;
    alglib::ae_int_t natural_bound_type = 2;

	//for(int i=0; i<nframe ; i++) {
        //spline.addPoint(x[i], y[i]);
	//}
	alglib::real_1d_array *rx = new alglib::real_1d_array();
	rx->setcontent(nframe, x);
	alglib::real_1d_array *ry = new alglib::real_1d_array();
	ry->setcontent(nframe, y);
	alglib::spline1dbuildcubic(
                            *const_cast<const alglib::real_1d_array*>(rx),
                            *const_cast<const alglib::real_1d_array*>(ry),
                            s);


	//wlasciwe interpolowanie funkcji
    int sizeAfterSpline = (int)(x[signalSize-1]/FREQ_SAMPLING) +1;
	double* sig = new double[sizeAfterSpline];



    for(int i=0; i<sizeAfterSpline; i++) {
        std::cout << sig[i] << "\n";
        //sig[i] = spline(i*FREQ_SAMPLING);
        sig[i] = alglib::spline1dcalc(s, i*FREQ_SAMPLING);
    }
    return sig;
}

double HRV1Analyzer::mean(double *tab, int start, int end) {
    double temp=0;

    for(int i=start; i<end; i++) {
		temp += tab[i];
	}
    return temp/(end-start);
}

double HRV1Analyzer::std(double *tab, int start, int end) {
    double temp=0;
    double avg = mean(tab, start, end);

    for(int i=start; i<end; i++) {
        temp += std::pow( (tab[i]-avg), 2);
    }
    return std::sqrt(temp/(end-start));
}




