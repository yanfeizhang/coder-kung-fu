<?php
class FioTest {

	private $_in = "read.fio";
	private $_out = "data/read_total";

	public function run(){		
		file_put_contents($this->_out, "");
		
		$sections = $this->getSections($this->_in);
		foreach($sections as $sec){
			$fioResult = $this->runFioTest($sec);	
			file_put_contents("data/".$sec, $fioResult);
			$res = $this->parseFioResult($fioResult);			

			$line = $sec."\t".$res['bw']."\t".$res['lat']."\t".$res['iops']."\n";
			echo $line;
			file_put_contents($this->_out, $line, FILE_APPEND);
			sleep(60);
		}
	}
	
	public function parseFioResult($fioResult){		
		$result = array();
		$fio = json_decode($fioResult, true);
		$result['iops'] = $fio["jobs"][0]['read']['iops_mean'];
		$result['bw'] = $fio["jobs"][0]['read']['bw_mean']; //kb
		$result['lat'] = $fio["jobs"][0]['read']["lat_ns"]['mean']; //ns
		return $result;
	}
	
	public function runFioTest($section){
		$cmd = "fio ".$this->_in." --section=".$section." --output-format=json";
		echo $cmd."\n";
		$result = shell_exec($cmd);
		return $result;
	}
	
	public function getSections($fioFile){
		$matches = array();
		$contents = file_get_contents($fioFile);
		$pattern = '/\[(.+)\]/';
		preg_match_all($pattern, $contents, $matches);

		$secs = array();
		foreach($matches[1] as $sec){
			if($sec=="global"){
				continue;
			}
			$secs[] = $sec;
		}
		return $secs;
	}	
}

$test = new FioTest();
$test->run();







